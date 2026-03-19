# UDialogueComponent

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)  
**File:** `Dialogue/Components/DialogueComponent.h / .cpp`  
**Base:** `UActorComponent`  
**Net role:** Server-authoritative. The component itself is replicated (`SetIsReplicated(true)`) so that its `NetGUID` is valid on clients — required for `FDialogueClientState::OwnerComponent` to deserialize correctly across RPCs.

---

## Responsibilities

- Owns all active `FDialogueSession` instances for its actor.
- Calls `FDialogueSimulator::Advance()` to run sessions.
- Pushes `FDialogueClientState` to participants via reliable `ClientRPC_ReceiveDialogueState` on each participant's `UDialogueManagerComponent`.
- Receives choice and ACK calls from `UDialogueManagerComponent` as direct server-side method calls (not UFunction RPCs) after the manager has validated and routed the incoming client RPC.
- Manages choice timeout timers.
- Enforces one active session per instigator (configurable via `bInterruptOnNewSession`).
- Broadcasts `OnDialogueStarted` / `OnDialogueEnded` for game-side listeners.

---

## Constructor — Replication

```cpp
UDialogueComponent::UDialogueComponent()
{
    // Must be true so the engine assigns a NetGUID to this component.
    // FDialogueClientState::OwnerComponent is an object reference carried in a ClientRPC;
    // without a NetGUID the pointer will be null on the receiving client.
    SetIsReplicated(true);

    // No tick needed — sessions are event-driven.
    PrimaryComponentTick.bCanEverTick = false;
}
```

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

    // Timer callback: auto-submit the default choice on expiry.
    void OnChoiceTimeout(FGuid SessionID, int32 DefaultChoiceIndex);

    // Builds the FDialogueClientState to send at a Wait step.
    FDialogueClientState BuildClientState(
        const FDialogueSession& Session,
        const FDialogueStepResult& StepResult,
        bool bIsObserver) const;

    // Resolves the UDialogueManagerComponent from an actor's owning PlayerState.
    // Returns nullptr if not found. Works for both pawns and player state actors.
    static UDialogueManagerComponent* GetManagerComponent(AActor* Actor);

    // Returns the SessionID for the given instigator, or an invalid guid if none.
    FGuid GetSessionIDForInstigator(const AActor* Instigator) const;

    // ── Called by UDialogueManagerComponent (direct server-side calls, not RPCs) ──
    // These are plain C++ methods. UDialogueManagerComponent is a friend and calls
    // them after validating and routing the incoming client RPC.

    void Server_ReceiveChoice(FGuid SessionID, AActor* Sender, int32 ChoiceIndex);
    void Server_ReceiveACK(FGuid SessionID, AActor* Sender);
    void Server_NotifyChooserDisconnect(FGuid SessionID);

    friend class UDialogueManagerComponent;
};
```

---

## Key Method Implementations

### StartGroupDialogue

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

    RunSession(ActiveSessions[NewSessionID]);
}
```

### RunSession

```cpp
void UDialogueComponent::RunSession(FDialogueSession& Session)
{
    const FDialogueStepResult Result = FDialogueSimulator::Advance(Session);

    switch (Result.Action)
    {
    case EDialogueStepAction::WaitForACK:
    case EDialogueStepAction::WaitForChoice:
    {
        PushClientState(Session, Result);

        if (Result.Action == EDialogueStepAction::WaitForChoice)
        {
            // CurrentNodeIndex still points at the waiting PlayerChoice node (see simulator invariant).
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
        }
        break;
    }

    case EDialogueStepAction::EndSession:
        EndSession(Session.SessionID, Result.EndReason);
        break;

    default:
        // Continue / SubDialoguePush are consumed inside FDialogueSimulator::Advance.
        // Reaching here means Advance returned an unexpected action — treat as error.
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
            TEXT("DialogueComponent"),
            FString::Printf(TEXT("RunSession: unexpected Result.Action %d returned from Advance."),
                static_cast<int32>(Result.Action)));
        EndSession(Session.SessionID, EDialogueEndReason::AssetError);
        break;
    }
}
```

### PushClientState

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
        // OwnerComponent carried in the state so the client can populate SessionOwners
        // without a separate registration RPC.
        State.OwnerComponent = this;

        if (UDialogueManagerComponent* Manager = GetManagerComponent(Participant))
            Manager->ClientRPC_ReceiveDialogueState(State);
    }
}
```

### Server_ReceiveChoice

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
    {
        // Locked or invalid choice. Session remains waiting — client should re-prompt.
        return;
    }

    // Advance CurrentNodeIndex to the choice target, then resume.
    Session->CurrentFrame().CurrentNodeIndex = TargetNode;
    Session->bWaiting = false;
    RunSession(*Session);
}
```

### Server_ReceiveACK

```cpp
void UDialogueComponent::Server_ReceiveACK(FGuid SessionID, AActor* Sender)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session || !Session->bWaiting) return;

    // Read NextNodeIndex from the waiting Line node directly — no secondary storage.
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

### StartTimeoutTimer

```cpp
void UDialogueComponent::StartTimeoutTimer(
    FDialogueSession& Session, float TimeoutSeconds, int32 DefaultChoiceIndex)
{
    // Capture SessionID by value — lambda must not hold a reference into the TMap.
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
    // Chooser is passed as the sender so the Chooser-check in Server_ReceiveChoice passes.
    Server_ReceiveChoice(SessionID, Session->GetChooser(), DefaultChoiceIndex);
}
```

### EndSession

```cpp
void UDialogueComponent::EndSession(FGuid SessionID, EDialogueEndReason Reason)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session) return;

    if (Session->TimeoutHandle.IsValid())
        GetWorld()->GetTimerManager().ClearTimer(Session->TimeoutHandle);

    for (const TWeakObjectPtr<AActor>& ParticipantWeak : Session->Participants)
    {
        if (AActor* P = ParticipantWeak.Get())
        {
            if (UDialogueManagerComponent* Manager = GetManagerComponent(P))
                Manager->ClientRPC_DialogueEnded(SessionID, Reason);
        }
    }

    OnDialogueEnded.Broadcast(SessionID, Reason);
    ActiveSessions.Remove(SessionID);
}
```

### EndPlay — Force-end all sessions on server shutdown

```cpp
void UDialogueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Copy keys to avoid modifying the map while iterating.
    TArray<FGuid> SessionIDs;
    ActiveSessions.GetKeys(SessionIDs);
    for (const FGuid& ID : SessionIDs)
        EndSession(ID, EDialogueEndReason::Interrupted);

    Super::EndPlay(EndPlayReason);
}
```

---

## Chooser Disconnect

`UDialogueManagerComponent::EndPlay` calls `Server_NotifyChooserDisconnect` directly as a server-side method call. No RPC round-trip is needed because `EndPlay` on `APlayerState` already executes on the server.

```cpp
void UDialogueComponent::Server_NotifyChooserDisconnect(FGuid SessionID)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session) return;

    // Only end the session if the disconnecting player was the Chooser.
    // Observers disconnecting do not end the session — they are simply removed.
    if (!Session->Chooser.IsValid())
    {
        // Chooser's weak ptr went null — treat as disconnect.
        EndSession(SessionID, EDialogueEndReason::ChooserDisconnected);
        return;
    }

    // If called before the weak ptr clears, check by session ID match.
    EndSession(SessionID, EDialogueEndReason::ChooserDisconnected);
}
```
