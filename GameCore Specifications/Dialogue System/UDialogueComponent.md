# UDialogueComponent

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)  
**File:** `Dialogue/Components/DialogueComponent.h / .cpp`  
**Base:** `UActorComponent`  
**Net role:** Server-authoritative. Exists on server only (do not replicate the component itself).

---

## Responsibilities

- Owns all active `FDialogueSession` instances for its actor.
- Calls `FDialogueSimulator::Advance()` to run sessions.
- Replicates `FDialogueClientState` to participants via RPCs on their `UDialogueManagerComponent`.
- Receives `ServerRPC_SubmitChoice` and `ServerRPC_AcknowledgeLine` from participants.
- Manages choice timeout timers.
- Enforces one active session per instigator (configurable).
- Broadcasts `OnDialogueStarted` / `OnDialogueEnded` for game-side listeners.

---

## Class Definition

```cpp
// File: Dialogue/Components/DialogueComponent.h

// Fired on the server when a session starts.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueStarted,
    FGuid, SessionID, AActor*, Instigator);

// Fired on the server when a session ends.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueEnded,
    FGuid, SessionID, EDialogueEndReason, Reason);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UDialogueComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDialogueComponent();

    // ── Configuration ────────────────────────────────────────────────────────

    // If true, starting a new dialogue for an instigator that already has an
    // active session will end the existing session before starting the new one.
    // If false, the new StartDialogue call is a no-op.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    bool bInterruptOnNewSession = false;

    // ── Activation API ───────────────────────────────────────────────────────

    // Start a single-player dialogue. Server only. Wraps instigator in a
    // one-element participants array and calls StartGroupDialogue.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dialogue")
    void StartDialogue(AActor* Instigator, UDialogueAsset* Asset);

    // Start a group dialogue. Server only.
    // Participants[0] is the default Chooser unless ResolveChooser is bound.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dialogue")
    void StartGroupDialogue(const TArray<AActor*>& Participants, UDialogueAsset* Asset);

    // Immediately end a session. Safe to call on an already-ended session.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dialogue")
    void ForceEndSession(FGuid SessionID);

    // Returns true if the given actor has an active session on this component.
    UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Dialogue")
    bool HasActiveSession(AActor* Instigator) const;

    // ── Chooser Resolution ───────────────────────────────────────────────────

    // Override to customise which participant becomes the Chooser.
    // Default: Participants[0].
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

    // Per-session: the node index of the UDialogueNode_PlayerChoice or Line node
    // currently waiting for input. Stored explicitly to avoid index arithmetic in the simulator.
    TMap<FGuid, int32> PendingChoiceNodeIndex;

    // ── Internal Flow ────────────────────────────────────────────────────────

    void RunSession(FDialogueSession& Session);
    void PushClientState(FDialogueSession& Session, const FDialogueStepResult& Result);
    void EndSession(FGuid SessionID, EDialogueEndReason Reason);
    void StartTimeoutTimer(FDialogueSession& Session, float TimeoutSeconds, int32 DefaultChoiceIndex);
    void OnChoiceTimeout(FGuid SessionID, int32 DefaultChoiceIndex);

    // Builds FDialogueClientState from the current session step result.
    FDialogueClientState BuildClientState(
        const FDialogueSession& Session,
        const FDialogueStepResult& Result,
        bool bIsObserver) const;

    // Resolves the UDialogueManagerComponent on an actor's PlayerState.
    // Returns nullptr if not found.
    static UDialogueManagerComponent* GetManagerComponent(AActor* Actor);

    // ── Incoming RPCs (called by UDialogueManagerComponent via cross-component call) ──
    // These are NOT UFunction RPCs — they are direct server-side calls from
    // UDialogueManagerComponent after it receives and validates the client RPC.

    void Server_ReceiveChoice(FGuid SessionID, AActor* Sender, int32 ChoiceIndex);
    void Server_ReceiveACK(FGuid SessionID, AActor* Sender);

    friend class UDialogueManagerComponent;
};
```

---

## Key Method Implementations

### StartGroupDialogue

```cpp
void UDialogueComponent::StartGroupDialogue(const TArray<AActor*>& Participants, UDialogueAsset* Asset)
{
    if (!HasAuthority()) return;
    if (!Asset || Participants.IsEmpty()) return;

    // Check for existing sessions per participant.
    for (AActor* P : Participants)
    {
        if (!P) continue;
        if (HasActiveSession(P))
        {
            if (bInterruptOnNewSession)
                ForceEndSession(GetSessionIDForInstigator(P));
            else
                return; // no-op
        }
    }

    // Build session.
    FDialogueSession Session;
    Session.SessionID = FGuid::NewGuid();
    for (AActor* P : Participants)
        if (P) Session.Participants.Add(P);

    // Resolve Chooser.
    AActor* ResolvedChooser = ResolveChooser.IsBound()
        ? ResolveChooser.Execute(Participants)
        : Participants[0];
    Session.Chooser = ResolvedChooser;

    // Push root frame.
    FDialogueStackFrame RootFrame;
    RootFrame.Asset            = Asset;
    RootFrame.CurrentNodeIndex = Asset->StartNodeIndex;
    RootFrame.ReturnNodeIndex  = INDEX_NONE;
    Session.AssetStack.Add(RootFrame);

    ActiveSessions.Add(Session.SessionID, Session);
    OnDialogueStarted.Broadcast(Session.SessionID, ResolvedChooser);

    RunSession(ActiveSessions[Session.SessionID]);
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
        PushClientState(Session, Result);
        if (Result.Action == EDialogueStepAction::WaitForChoice)
        {
            // Cache which node is waiting so Server_ReceiveChoice can find it.
            const UDialogueNode_PlayerChoice* ChoiceNode =
                Cast<UDialogueNode_PlayerChoice>(
                    Session.CurrentFrame().Asset->GetNode(Session.CurrentFrame().CurrentNodeIndex));
            if (ChoiceNode && ChoiceNode->TimeoutSeconds > 0.0f)
            {
                StartTimeoutTimer(Session, ChoiceNode->TimeoutSeconds, ChoiceNode->DefaultChoiceIndex);
            }
        }
        break;

    case EDialogueStepAction::EndSession:
        EndSession(Session.SessionID, Result.EndReason);
        break;

    default:
        // Continue / SubDialoguePush are handled inside FDialogueSimulator::Advance.
        break;
    }
}
```

### PushClientState

```cpp
void UDialogueComponent::PushClientState(FDialogueSession& Session, const FDialogueStepResult& Result)
{
    for (const TWeakObjectPtr<AActor>& ParticipantWeak : Session.Participants)
    {
        AActor* Participant = ParticipantWeak.Get();
        if (!Participant) continue;

        const bool bIsObserver = (Participant != Session.GetChooser());
        FDialogueClientState State = BuildClientState(Session, Result, bIsObserver);

        if (UDialogueManagerComponent* Manager = GetManagerComponent(Participant))
        {
            Manager->ClientRPC_ReceiveDialogueState(State);
        }
    }
}
```

### Server_ReceiveChoice

```cpp
void UDialogueComponent::Server_ReceiveChoice(FGuid SessionID, AActor* Sender, int32 ChoiceIndex)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session) return;

    // Only the Chooser may submit choices.
    if (Session->GetChooser() != Sender)
    {
        UE_LOG(LogDialogue, Warning, TEXT("Non-Chooser submitted choice — ignored."));
        return;
    }

    if (!Session->bWaiting) return;

    // Cancel timeout timer if running.
    if (Session->TimeoutHandle.IsValid())
        GetWorld()->GetTimerManager().ClearTimer(Session->TimeoutHandle);

    const int32 TargetNode = FDialogueSimulator::ResolveChoice(*Session, ChoiceIndex);
    if (TargetNode == INDEX_NONE)
    {
        // Invalid or locked choice — no-op. The session remains waiting.
        return;
    }

    Session->CurrentFrame().CurrentNodeIndex = TargetNode;
    Session->bWaiting = false;
    RunSession(*Session);
}
```

### EndSession

```cpp
void UDialogueComponent::EndSession(FGuid SessionID, EDialogueEndReason Reason)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session) return;

    // Clear timeout timer.
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
    ActiveSessions.Remove(SessionID);
    PendingChoiceNodeIndex.Remove(SessionID);
}
```

---

## Chooser Disconnect Handling

`UDialogueComponent` does not poll for disconnected actors. Instead, `UDialogueManagerComponent` on the departing player's `APlayerState` calls `Server_NotifyDisconnect(SessionID)` from its `EndPlay`. The component then calls `EndSession(SessionID, EDialogueEndReason::ChooserDisconnected)` if the disconnecting actor is the Chooser.

```cpp
void UDialogueComponent::Server_NotifyChooserDisconnect(FGuid SessionID)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (Session)
        EndSession(SessionID, EDialogueEndReason::ChooserDisconnected);
}
```
