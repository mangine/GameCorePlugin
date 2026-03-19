# UDialogueManagerComponent

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)  
**File:** `Dialogue/Components/DialogueManagerComponent.h / .cpp`  
**Base:** `UActorComponent`  
**Placed on:** `APlayerState`

---

## Responsibilities

- Receives `FDialogueClientState` pushed from `UDialogueComponent` via reliable ClientRPC.
- Broadcasts UI-facing delegates so widgets bind without knowledge of `UDialogueComponent`.
- Sends `ServerRPC_SubmitChoice` and `ServerRPC_AcknowledgeLine` to the NPC's `UDialogueComponent`.
- On `EndPlay` (player disconnect), notifies all active `UDialogueComponent` instances that this player has left, so Chooser-disconnect handling fires correctly.
- Handles multiple concurrent sessions (rare, but valid in group content).

---

## Class Definition

```cpp
// File: Dialogue/Components/DialogueManagerComponent.h

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueStateReceived,
    const FDialogueClientState&, State);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueSessionEnded,
    FGuid, SessionID,
    EDialogueEndReason, Reason);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UDialogueManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDialogueManagerComponent();

    // ── Delegates (UI binds here) ─────────────────────────────────────────────

    // Fires on the owning client whenever new dialogue state arrives.
    // UI widget binds here to update speaker, text, and choices.
    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueStateReceived OnDialogueStateReceived;

    // Fires on the owning client when a session ends.
    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueSessionEnded OnDialogueSessionEnded;

    // ── Client API (called by UI) ─────────────────────────────────────────────

    // Submit a choice for the given session. Validates that the session is active
    // and the component is not in Observer mode before sending the RPC.
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void SubmitChoice(FGuid SessionID, int32 ChoiceIndex);

    // Acknowledge the current line for the given session.
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void AcknowledgeLine(FGuid SessionID);

    // Returns the most recent FDialogueClientState for a session. Nullable.
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    bool GetActiveState(FGuid SessionID, FDialogueClientState& OutState) const;

    // ── UActorComponent ──────────────────────────────────────────────────────

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ── Server RPCs ──────────────────────────────────────────────────────────

    UFUNCTION(Server, Reliable)
    void ServerRPC_SubmitChoice(FGuid SessionID, int32 ChoiceIndex,
                                UDialogueComponent* TargetComponent);

    UFUNCTION(Server, Reliable)
    void ServerRPC_AcknowledgeLine(FGuid SessionID,
                                   UDialogueComponent* TargetComponent);

    // Called by UDialogueComponent on the server to notify this player has left a session.
    UFUNCTION(Server, Reliable)
    void ServerRPC_NotifyDisconnect(FGuid SessionID, UDialogueComponent* TargetComponent);

    // ── Client RPCs (called by UDialogueComponent on the server) ─────────────

    UFUNCTION(Client, Reliable)
    void ClientRPC_ReceiveDialogueState(const FDialogueClientState& State);

    UFUNCTION(Client, Reliable)
    void ClientRPC_DialogueEnded(FGuid SessionID, EDialogueEndReason Reason);

private:
    // Most recent state per active session. Keyed by SessionID.
    TMap<FGuid, FDialogueClientState> ActiveStates;

    // Track which UDialogueComponent owns each session so disconnect RPCs route correctly.
    TMap<FGuid, TWeakObjectPtr<UDialogueComponent>> SessionOwners;
};
```

---

## Key Method Implementations

### ClientRPC_ReceiveDialogueState

```cpp
void UDialogueManagerComponent::ClientRPC_ReceiveDialogueState_Implementation(
    const FDialogueClientState& State)
{
    ActiveStates.Add(State.SessionID, State);
    OnDialogueStateReceived.Broadcast(State);
}
```

### ClientRPC_DialogueEnded

```cpp
void UDialogueManagerComponent::ClientRPC_DialogueEnded_Implementation(
    FGuid SessionID, EDialogueEndReason Reason)
{
    ActiveStates.Remove(SessionID);
    SessionOwners.Remove(SessionID);
    OnDialogueSessionEnded.Broadcast(SessionID, Reason);
}
```

### SubmitChoice

```cpp
void UDialogueManagerComponent::SubmitChoice(FGuid SessionID, int32 ChoiceIndex)
{
    const FDialogueClientState* State = ActiveStates.Find(SessionID);
    if (!State) return;

    // Silently suppress if Observer — do not send RPC.
    if (State->bIsObserver) return;
    if (!State->bWaitingForChoice) return;

    const TWeakObjectPtr<UDialogueComponent>* OwnerWeak = SessionOwners.Find(SessionID);
    if (!OwnerWeak || !OwnerWeak->IsValid()) return;

    ServerRPC_SubmitChoice(SessionID, ChoiceIndex, OwnerWeak->Get());
}
```

### ServerRPC_SubmitChoice

```cpp
void UDialogueManagerComponent::ServerRPC_SubmitChoice_Implementation(
    FGuid SessionID, int32 ChoiceIndex, UDialogueComponent* TargetComponent)
{
    if (!TargetComponent) return;
    APawn* OwnerPawn = Cast<APlayerState>(GetOwner()) ?
        Cast<APlayerState>(GetOwner())->GetPawn() : nullptr;
    if (!OwnerPawn) return;

    TargetComponent->Server_ReceiveChoice(SessionID, OwnerPawn, ChoiceIndex);
}
```

### EndPlay — Disconnect notification

```cpp
void UDialogueManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Notify all UDialogueComponents that this player is leaving their sessions.
    for (auto& Pair : SessionOwners)
    {
        if (UDialogueComponent* DC = Pair.Value.Get())
        {
            ServerRPC_NotifyDisconnect(Pair.Key, DC);
        }
    }
    Super::EndPlay(EndPlayReason);
}
```

> **Note:** `ServerRPC_NotifyDisconnect` is called from `EndPlay` which runs on the server when the connection closes. On the server, `ServerRPC_*` functions are called locally, so this correctly reaches `UDialogueComponent::Server_NotifyChooserDisconnect` without a round-trip.

---

## Session Owner Registration

`UDialogueComponent::PushClientState` calls `Manager->ClientRPC_ReceiveDialogueState`, which is a client RPC and does not return a value. The `SessionOwners` map on `UDialogueManagerComponent` must be populated separately.

`UDialogueComponent` calls a server-side direct method (not an RPC) on `UDialogueManagerComponent` to register the session owner when a session starts:

```cpp
// In UDialogueComponent::RunSession, after confirming the manager exists:
void UDialogueComponent::RegisterSessionOwner(UDialogueManagerComponent* Manager, FGuid SessionID)
{
    // Direct server-to-server call — no RPC needed.
    Manager->Server_RegisterSessionOwner(SessionID, this);
}

// On UDialogueManagerComponent:
void UDialogueManagerComponent::Server_RegisterSessionOwner(
    FGuid SessionID, UDialogueComponent* Owner)
{
    SessionOwners.Add(SessionID, Owner);
}
```
