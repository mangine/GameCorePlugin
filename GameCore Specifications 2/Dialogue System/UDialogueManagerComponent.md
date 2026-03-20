# UDialogueManagerComponent

**File:** `Dialogue/Components/DialogueManagerComponent.h / .cpp`  
**Base:** `UActorComponent`  
**Placed on:** `APlayerState`

---

## Responsibilities

- Receives `FDialogueClientState` from `UDialogueComponent` via reliable `ClientRPC_ReceiveDialogueState`.
- Populates `SessionOwners` automatically from `FDialogueClientState::OwnerComponent` — no separate registration RPC.
- Broadcasts UI-facing delegates so widgets bind without any knowledge of `UDialogueComponent`.
- Sends `ServerRPC_SubmitChoice` and `ServerRPC_AcknowledgeLine` to the server; the server-side implementation calls `UDialogueComponent` directly as C++ method calls.
- On `EndPlay` (player disconnect), notifies all active `UDialogueComponent` instances via direct C++ calls — no RPC round-trip.
- Handles multiple concurrent sessions (rare but valid in group content).

---

## Class Definition

```cpp
// File: Dialogue/Components/DialogueManagerComponent.h

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueStateReceived,
    const FDialogueClientState&, State);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueSessionEnded,
    FGuid,              SessionID,
    EDialogueEndReason, Reason);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UDialogueManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDialogueManagerComponent();

    // ── Delegates (UI binds here) ─────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueStateReceived OnDialogueStateReceived;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueSessionEnded OnDialogueSessionEnded;

    // ── Client API (called by UI) ─────────────────────────────────────────────

    // Submit a choice for the given session.
    // Suppressed if bIsObserver or session is not waiting for a choice.
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void SubmitChoice(FGuid SessionID, int32 ChoiceIndex);

    // Acknowledge the current line for the given session.
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void AcknowledgeLine(FGuid SessionID);

    // Returns the most recent FDialogueClientState for a session.
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    bool GetActiveState(FGuid SessionID, FDialogueClientState& OutState) const;

    // ── UActorComponent ──────────────────────────────────────────────────────

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ── Server RPCs ──────────────────────────────────────────────────────────

    // Routes choice to the owning UDialogueComponent as a direct server-side call.
    UFUNCTION(Server, Reliable)
    void ServerRPC_SubmitChoice(FGuid SessionID, int32 ChoiceIndex,
                                UDialogueComponent* TargetComponent);

    // Routes ACK to the owning UDialogueComponent as a direct server-side call.
    UFUNCTION(Server, Reliable)
    void ServerRPC_AcknowledgeLine(FGuid SessionID,
                                   UDialogueComponent* TargetComponent);

    // ── Client RPCs (called by UDialogueComponent on the server) ─────────────

    UFUNCTION(Client, Reliable)
    void ClientRPC_ReceiveDialogueState(const FDialogueClientState& State);

    UFUNCTION(Client, Reliable)
    void ClientRPC_DialogueEnded(FGuid SessionID, EDialogueEndReason Reason);

private:
    // Most recent client state per session.
    TMap<FGuid, FDialogueClientState> ActiveStates;

    // Maps SessionID → owning UDialogueComponent.
    // Populated from FDialogueClientState::OwnerComponent on ClientRPC receipt.
    TMap<FGuid, TWeakObjectPtr<UDialogueComponent>> SessionOwners;
};
```

---

## ClientRPC_ReceiveDialogueState

```cpp
void UDialogueManagerComponent::ClientRPC_ReceiveDialogueState_Implementation(
    const FDialogueClientState& State)
{
    ActiveStates.Add(State.SessionID, State);

    // Populate SessionOwners from the carried OwnerComponent reference.
    // UDialogueComponent is replicated (SetIsReplicated(true)) so OwnerComponent
    // is valid on the client when this RPC executes.
    if (State.OwnerComponent)
        SessionOwners.Add(State.SessionID, State.OwnerComponent);

    OnDialogueStateReceived.Broadcast(State);
}
```

---

## ClientRPC_DialogueEnded

```cpp
void UDialogueManagerComponent::ClientRPC_DialogueEnded_Implementation(
    FGuid SessionID, EDialogueEndReason Reason)
{
    ActiveStates.Remove(SessionID);
    SessionOwners.Remove(SessionID);
    OnDialogueSessionEnded.Broadcast(SessionID, Reason);
}
```

---

## SubmitChoice

```cpp
void UDialogueManagerComponent::SubmitChoice(FGuid SessionID, int32 ChoiceIndex)
{
    const FDialogueClientState* State = ActiveStates.Find(SessionID);
    if (!State) return;
    if (State->bIsObserver)        return; // Observers cannot submit choices.
    if (!State->bWaitingForChoice) return;

    const TWeakObjectPtr<UDialogueComponent>* OwnerWeak = SessionOwners.Find(SessionID);
    if (!OwnerWeak || !OwnerWeak->IsValid()) return;

    ServerRPC_SubmitChoice(SessionID, ChoiceIndex, OwnerWeak->Get());
}
```

---

## AcknowledgeLine

```cpp
void UDialogueManagerComponent::AcknowledgeLine(FGuid SessionID)
{
    const FDialogueClientState* State = ActiveStates.Find(SessionID);
    if (!State || !State->bWaitingForACK) return;

    const TWeakObjectPtr<UDialogueComponent>* OwnerWeak = SessionOwners.Find(SessionID);
    if (!OwnerWeak || !OwnerWeak->IsValid()) return;

    ServerRPC_AcknowledgeLine(SessionID, OwnerWeak->Get());
}
```

---

## ServerRPC_SubmitChoice

```cpp
void UDialogueManagerComponent::ServerRPC_SubmitChoice_Implementation(
    FGuid SessionID, int32 ChoiceIndex, UDialogueComponent* TargetComponent)
{
    if (!TargetComponent) return;

    APlayerState* PS = Cast<APlayerState>(GetOwner());
    APawn* OwnerPawn = PS ? PS->GetPawn() : nullptr;
    if (!OwnerPawn)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
            TEXT("DialogueManagerComponent"),
            TEXT("ServerRPC_SubmitChoice: owning pawn not found."));
        return;
    }

    TargetComponent->Server_ReceiveChoice(SessionID, OwnerPawn, ChoiceIndex);
}
```

---

## ServerRPC_AcknowledgeLine

```cpp
void UDialogueManagerComponent::ServerRPC_AcknowledgeLine_Implementation(
    FGuid SessionID, UDialogueComponent* TargetComponent)
{
    if (!TargetComponent) return;

    APlayerState* PS = Cast<APlayerState>(GetOwner());
    APawn* OwnerPawn = PS ? PS->GetPawn() : nullptr;
    if (!OwnerPawn) return;

    TargetComponent->Server_ReceiveACK(SessionID, OwnerPawn);
}
```

---

## GetActiveState

```cpp
bool UDialogueManagerComponent::GetActiveState(
    FGuid SessionID, FDialogueClientState& OutState) const
{
    if (const FDialogueClientState* State = ActiveStates.Find(SessionID))
    {
        OutState = *State;
        return true;
    }
    return false;
}
```

---

## EndPlay — Disconnect Notification

```cpp
void UDialogueManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Executes on the server when the player connection closes.
    // Direct method call — no RPC round-trip needed because EndPlay on APlayerState
    // already executes on the server. Calling a Server RPC from EndPlay is undefined
    // because the net connection may already be closed.
    for (auto& Pair : SessionOwners)
    {
        if (UDialogueComponent* DC = Pair.Value.Get())
            DC->Server_NotifyChooserDisconnect(Pair.Key);
    }

    Super::EndPlay(EndPlayReason);
}
```

> **Note:** This calls `Server_NotifyChooserDisconnect` for all sessions, regardless of whether the disconnecting player was the Chooser or an Observer. See KI-3 in Architecture.md — a fix should check the session's Chooser before ending it.
