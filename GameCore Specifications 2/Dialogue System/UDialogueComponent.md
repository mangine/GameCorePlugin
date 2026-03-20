# UDialogueComponent

**File:** `Dialogue/Components/DialogueComponent.h / .cpp`  
**Base:** `UActorComponent`  
**Net role:** Server-authoritative. The component is replicated (`SetIsReplicated(true)`) so its `NetGUID` is valid on clients, which is required for `FDialogueClientState::OwnerComponent` to deserialize correctly across RPCs.

---

## Responsibilities

- Owns all active `FDialogueSession` instances for its actor.
- Calls `FDialogueSimulator::Advance()` to run sessions.
- Pushes `FDialogueClientState` to participants via reliable `ClientRPC_ReceiveDialogueState` on each participant's `UDialogueManagerComponent`.
- Receives choice and ACK calls from `UDialogueManagerComponent` as direct C++ method calls after the manager has routed the incoming server RPC.
- Manages choice timeout timers.
- Enforces one active session per instigator (configurable).
- Broadcasts `OnDialogueStarted` / `OnDialogueEnded`.
- Broadcasts GMS events for session start and end.

---

## Class Definition

```cpp
// File: Dialogue/Components/DialogueComponent.h

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueStarted,
    FGuid,             SessionID,
    AActor*,           Instigator);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueEnded,
    FGuid,             SessionID,
    EDialogueEndReason, Reason);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UDialogueComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDialogueComponent();

    // ── Configuration ────────────────────────────────────────────────────────

    // If true, starting a new session for an instigator that already has an active
    // session ends the existing one. If false, the call is a silent no-op.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    bool bInterruptOnNewSession = false;

    // ── Activation API (server only) ─────────────────────────────────────────

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dialogue")
    void StartDialogue(AActor* Instigator, UDialogueAsset* Asset);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dialogue")
    void StartGroupDialogue(const TArray<AActor*>& Participants, UDialogueAsset* Asset);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dialogue")
    void ForceEndSession(FGuid SessionID);

    UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Dialogue")
    bool HasActiveSession(const AActor* Instigator) const;

    // ── Chooser Resolution ───────────────────────────────────────────────────

    // Defaults to returning Participants[0].
    // Game code overrides to implement group-leader-as-Chooser or similar policies.
    DECLARE_DELEGATE_RetVal_OneParam(AActor*, FResolveChooserDelegate, const TArray<AActor*>&);
    FResolveChooserDelegate ResolveChooser;

    // ── Delegates ────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueStarted OnDialogueStarted;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueEnded OnDialogueEnded;

    // ── UActorComponent ──────────────────────────────────────────────────────

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // Active sessions keyed by SessionID.
    TMap<FGuid, FDialogueSession> ActiveSessions;

    // ── Internal Flow ────────────────────────────────────────────────────────

    void RunSession(FDialogueSession& Session);
    void PushClientState(FDialogueSession& Session, const FDialogueStepResult& StepResult);
    void EndSession(FGuid SessionID, EDialogueEndReason Reason);
    void StartTimeoutTimer(FDialogueSession& Session, float TimeoutSeconds, int32 DefaultChoiceIndex);
    void OnChoiceTimeout(FGuid SessionID, int32 DefaultChoiceIndex);

    FDialogueClientState BuildClientState(
        const FDialogueSession& Session,
        const FDialogueStepResult& StepResult,
        bool bIsObserver) const;

    static UDialogueManagerComponent* GetManagerComponent(AActor* Actor);

    FGuid GetSessionIDForInstigator(const AActor* Instigator) const;

    // ── Called by UDialogueManagerComponent (direct C++ calls, not RPCs) ────
    void Server_ReceiveChoice(FGuid SessionID, AActor* Sender, int32 ChoiceIndex);
    void Server_ReceiveACK(FGuid SessionID, AActor* Sender);
    void Server_NotifyChooserDisconnect(FGuid SessionID);

    friend class UDialogueManagerComponent;
};
```

---

## Constructor

```cpp
UDialogueComponent::UDialogueComponent()
{
    // Must be true so the engine assigns a NetGUID to this component.
    // FDialogueClientState::OwnerComponent is an object reference in a ClientRPC;
    // without NetGUID the pointer will be null on the receiving client.
    SetIsReplicated(true);
    PrimaryComponentTick.bCanEverTick = false;
}
```

---

## StartDialogue

```cpp
void UDialogueComponent::StartDialogue(AActor* Instigator, UDialogueAsset* Asset)
{
    if (!HasAuthority()) return;
    // Wrap the single instigator in an array and use the same group path.
    StartGroupDialogue({ Instigator }, Asset);
}
```

---

## StartGroupDialogue

```cpp
void UDialogueComponent::StartGroupDialogue(
    const TArray<AActor*>& Participants, UDialogueAsset* Asset)
{
    if (!HasAuthority()) return;

    if (!Asset)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
            TEXT("DialogueComponent"), TEXT("StartGroupDialogue called with null Asset."));
        return;
    }
    if (Participants.IsEmpty())
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
            TEXT("DialogueComponent"), TEXT("StartGroupDialogue called with empty Participants."));
        return;
    }

    for (AActor* P : Participants)
    {
        if (!P) continue;
        if (HasActiveSession(P))
        {
            if (bInterruptOnNewSession)
                ForceEndSession(GetSessionIDForInstigator(P));
            else
                return;
        }
    }

    FDialogueSession Session;
    Session.SessionID = FGuid::NewGuid();
    for (AActor* P : Participants)
        if (P) Session.Participants.Add(P);

    AActor* ResolvedChooser = ResolveChooser.IsBound()
        ? ResolveChooser.Execute(Participants)
        : Participants[0];

    if (!ResolvedChooser)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
            TEXT("DialogueComponent"),
            TEXT("StartGroupDialogue: ResolveChooser returned null. Session aborted."));
        return;
    }
    Session.Chooser = ResolvedChooser;

    FDialogueStackFrame RootFrame;
    RootFrame.Asset            = Asset;
    RootFrame.CurrentNodeIndex = Asset->StartNodeIndex;
    RootFrame.ReturnNodeIndex  = INDEX_NONE;
    Session.AssetStack.Add(RootFrame);

    const FGuid NewSessionID = Session.SessionID;
    ActiveSessions.Add(NewSessionID, MoveTemp(Session));
    OnDialogueStarted.Broadcast(NewSessionID, ResolvedChooser);

    // Broadcast GMS session started event.
    FDialogueSessionEventMessage StartMsg;
    StartMsg.SessionID  = NewSessionID;
    StartMsg.Instigator = ResolvedChooser;
    StartMsg.Asset      = Asset;
    UGameplayMessageSubsystem::Get(GetWorld())
        .BroadcastMessage(TAG_DialogueEvent_Session_Started, StartMsg);

    RunSession(ActiveSessions[NewSessionID]);
}
```

---

## RunSession

```cpp
void UDialogueComponent::RunSession(FDialogueSession& Session)
{
    const FDialogueStepResult Result = FDialogueSimulator::Advance(Session);

    switch (Result.Action)
    {
    case EDialogueStepAction::WaitForACK:
        // Always push client state before waiting — clients must see the line.
        PushClientState(Session, Result);
        break;

    case EDialogueStepAction::WaitForChoice:
    {
        PushClientState(Session, Result);

        // CurrentNodeIndex still points at the waiting PlayerChoice node.
        const UDialogueNode_PlayerChoice* ChoiceNode =
            Cast<UDialogueNode_PlayerChoice>(
                Session.CurrentFrame().Asset
                    ->GetNode(Session.CurrentFrame().CurrentNodeIndex));

        if (ChoiceNode && ChoiceNode->TimeoutSeconds > 0.0f)
        {
            StartTimeoutTimer(
                Session,
                ChoiceNode->TimeoutSeconds,
                ChoiceNode->DefaultChoiceIndex);
        }
        break;
    }

    case EDialogueStepAction::EndSession:
        EndSession(Session.SessionID, Result.EndReason);
        break;

    default:
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
            TEXT("DialogueComponent"),
            FString::Printf(
                TEXT("RunSession: unexpected Result.Action %d returned from Advance."),
                static_cast<int32>(Result.Action)));
        EndSession(Session.SessionID, EDialogueEndReason::AssetError);
        break;
    }
}
```

---

## BuildClientState

Reads display data from the **currently waiting** node at `Session.CurrentFrame().CurrentNodeIndex`. This is safe because the simulator invariant guarantees `CurrentNodeIndex` is always at the waiting node when `RunSession` calls this.

```cpp
FDialogueClientState UDialogueComponent::BuildClientState(
    const FDialogueSession& Session,
    const FDialogueStepResult& StepResult,
    bool bIsObserver) const
{
    FDialogueClientState State;
    State.SessionID     = Session.SessionID;
    State.bIsObserver   = bIsObserver;
    State.OwnerComponent = const_cast<UDialogueComponent*>(this);

    const UDialogueNode* WaitingNode =
        Session.CurrentFrame().Asset->GetNode(Session.CurrentFrame().CurrentNodeIndex);

    if (StepResult.Action == EDialogueStepAction::WaitForACK)
    {
        const UDialogueNode_Line* LineNode = Cast<UDialogueNode_Line>(WaitingNode);
        if (LineNode)
        {
            State.SpeakerTag      = LineNode->SpeakerTag;
            State.LineText        = LineNode->LineText;
            State.VoiceCue        = LineNode->VoiceCue;
            State.bWaitingForACK  = true;
        }
    }
    else if (StepResult.Action == EDialogueStepAction::WaitForChoice)
    {
        const UDialogueNode_PlayerChoice* ChoiceNode =
            Cast<UDialogueNode_PlayerChoice>(WaitingNode);
        if (ChoiceNode)
        {
            State.bWaitingForChoice = true;
            State.TimeoutSeconds    = ChoiceNode->TimeoutSeconds;

            FRequirementContext Ctx = FDialogueSimulator::BuildContext(Session);
            for (int32 i = 0; i < ChoiceNode->Choices.Num(); ++i)
            {
                const FDialogueChoiceConfig& Cfg = ChoiceNode->Choices[i];
                FDialogueClientChoice ClientChoice;
                ClientChoice.ChoiceIndex    = i;
                ClientChoice.Label          = Cfg.Label;
                if (Cfg.LockCondition)
                {
                    const bool bPassed =
                        URequirementLibrary::EvaluateRequirement(Cfg.LockCondition, Ctx).bPassed;
                    ClientChoice.bLocked       = !bPassed;
                    ClientChoice.LockReasonText = !bPassed ? Cfg.LockReasonText : FText::GetEmpty();
                }
                State.Choices.Add(ClientChoice);
            }

            // Set remaining timeout from the active timer if one is running.
            if (Session.TimeoutHandle.IsValid() && ChoiceNode->TimeoutSeconds > 0.0f)
            {
                State.TimeoutRemainingSeconds =
                    GetWorld()->GetTimerManager().GetTimerRemaining(Session.TimeoutHandle);
            }
        }
    }

    return State;
}
```

---

## PushClientState

```cpp
void UDialogueComponent::PushClientState(
    FDialogueSession& Session, const FDialogueStepResult& StepResult)
{
    for (const TWeakObjectPtr<AActor>& ParticipantWeak : Session.Participants)
    {
        AActor* Participant = ParticipantWeak.Get();
        if (!Participant) continue;

        const bool bIsObserver = (Participant != Session.GetChooser());
        FDialogueClientState State = BuildClientState(Session, StepResult, bIsObserver);

        if (UDialogueManagerComponent* Manager = GetManagerComponent(Participant))
            Manager->ClientRPC_ReceiveDialogueState(State);
    }
}
```

---

## Server_ReceiveChoice

```cpp
void UDialogueComponent::Server_ReceiveChoice(
    FGuid SessionID, AActor* Sender, int32 ChoiceIndex)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session) return;

    if (Session->GetChooser() != Sender)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
            TEXT("DialogueComponent"),
            FString::Printf(
                TEXT("Server_ReceiveChoice: non-Chooser actor '%s' submitted a choice — ignored."),
                *GetNameSafe(Sender)));
        return;
    }

    if (!Session->bWaiting) return;

    if (Session->TimeoutHandle.IsValid())
        GetWorld()->GetTimerManager().ClearTimer(Session->TimeoutHandle);

    const int32 TargetNode = FDialogueSimulator::ResolveChoice(*Session, ChoiceIndex);
    if (TargetNode == INDEX_NONE)
        return; // Locked or invalid choice — session remains waiting.

    Session->CurrentFrame().CurrentNodeIndex = TargetNode;
    Session->bWaiting = false;
    RunSession(*Session);
}
```

---

## Server_ReceiveACK

```cpp
void UDialogueComponent::Server_ReceiveACK(FGuid SessionID, AActor* Sender)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session || !Session->bWaiting) return;

    // Validate sender is a participant in this session.
    const bool bIsParticipant = Session->Participants.ContainsByPredicate(
        [Sender](const TWeakObjectPtr<AActor>& P) { return P.Get() == Sender; });
    if (!bIsParticipant)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
            TEXT("DialogueComponent"),
            FString::Printf(
                TEXT("Server_ReceiveACK: non-participant actor '%s' — ignored."),
                *GetNameSafe(Sender)));
        return;
    }

    const UDialogueNode_Line* LineNode =
        Cast<UDialogueNode_Line>(
            Session->CurrentFrame().Asset
                ->GetNode(Session->CurrentFrame().CurrentNodeIndex));

    if (!LineNode)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
            TEXT("DialogueComponent"),
            TEXT("Server_ReceiveACK: current node is not a Line node."));
        return;
    }

    Session->CurrentFrame().CurrentNodeIndex = LineNode->NextNodeIndex;
    Session->bWaiting = false;
    RunSession(*Session);
}
```

---

## StartTimeoutTimer / OnChoiceTimeout

```cpp
void UDialogueComponent::StartTimeoutTimer(
    FDialogueSession& Session, float TimeoutSeconds, int32 DefaultChoiceIndex)
{
    const FGuid CapturedID = Session.SessionID;
    GetWorld()->GetTimerManager().SetTimer(
        Session.TimeoutHandle,
        FTimerDelegate::CreateUObject(
            this, &UDialogueComponent::OnChoiceTimeout,
            CapturedID, DefaultChoiceIndex),
        TimeoutSeconds,
        /*bLoop=*/false);
}

void UDialogueComponent::OnChoiceTimeout(FGuid SessionID, int32 DefaultChoiceIndex)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session || !Session->bWaiting) return;

    FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
        TEXT("DialogueComponent"),
        FString::Printf(
            TEXT("Choice timeout in session %s — auto-selecting default choice %d."),
            *SessionID.ToString(), DefaultChoiceIndex));

    // Auto-submit through the same validated path as a player choice.
    // Chooser is passed as Sender so the Chooser check in Server_ReceiveChoice passes.
    Server_ReceiveChoice(SessionID, Session->GetChooser(), DefaultChoiceIndex);
}
```

---

## EndSession

```cpp
void UDialogueComponent::EndSession(FGuid SessionID, EDialogueEndReason Reason)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session) return;

    if (Session->TimeoutHandle.IsValid())
        GetWorld()->GetTimerManager().ClearTimer(Session->TimeoutHandle);

    // Notify all participants.
    for (const TWeakObjectPtr<AActor>& ParticipantWeak : Session->Participants)
    {
        if (AActor* P = ParticipantWeak.Get())
        {
            if (UDialogueManagerComponent* Manager = GetManagerComponent(P))
                Manager->ClientRPC_DialogueEnded(SessionID, Reason);
        }
    }

    OnDialogueEnded.Broadcast(SessionID, Reason);

    // Broadcast GMS session ended event.
    FDialogueSessionEndedMessage EndMsg;
    EndMsg.SessionID  = SessionID;
    EndMsg.Instigator = Session->GetChooser();
    EndMsg.Reason     = Reason;
    // Read EndReasonTag from UDialogueNode_End if the session completed naturally.
    if (Reason == EDialogueEndReason::Completed && !Session->AssetStack.IsEmpty())
    {
        const UDialogueNode_End* EndNode = Cast<UDialogueNode_End>(
            Session->CurrentFrame().Asset
                ->GetNode(Session->CurrentFrame().CurrentNodeIndex));
        if (EndNode)
            EndMsg.EndReasonTag = EndNode->EndReasonTag;
    }
    UGameplayMessageSubsystem::Get(GetWorld())
        .BroadcastMessage(TAG_DialogueEvent_Session_Ended, EndMsg);

    ActiveSessions.Remove(SessionID);
}
```

---

## EndPlay

```cpp
void UDialogueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Force-end all sessions on shutdown.
    TArray<FGuid> SessionIDs;
    ActiveSessions.GetKeys(SessionIDs);
    for (const FGuid& ID : SessionIDs)
        EndSession(ID, EDialogueEndReason::Interrupted);

    Super::EndPlay(EndPlayReason);
}
```

---

## Server_NotifyChooserDisconnect

```cpp
void UDialogueComponent::Server_NotifyChooserDisconnect(FGuid SessionID)
{
    EndSession(SessionID, EDialogueEndReason::ChooserDisconnected);
}
```

> **Why not handle Observer disconnects here?** `UDialogueManagerComponent::EndPlay` calls this for all sessions in `SessionOwners`. The current implementation ends the session unconditionally — KI-3 in Architecture.md flags this as a known issue. A future fix should check whether the disconnecting player was the Chooser or an Observer before calling `EndSession`.
