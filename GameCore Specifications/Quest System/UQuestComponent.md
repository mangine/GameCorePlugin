# UQuestComponent

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**File:** `Quest/Components/QuestComponent.h / .cpp` 
**Type:** `UActorComponent` on `APlayerState` 
**Implements:** `IPersistableComponent`

The per-player solo quest runtime. Zero knowledge of groups, parties, or sharing. Exposes a public server-side API for tracker increments — has no GMS subscriptions of its own.

`USharedQuestComponent` inherits this class and adds group-aware behavior as a fully optional extension.

---

## Class Declaration

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnQuestEventDelegate, FGameplayTag, QuestId, EQuestEventType, EventType);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnTrackerUpdatedDelegate,
    FGameplayTag, QuestId, FGameplayTag, TrackerKey, int32, NewValue);

UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UQuestComponent
    : public UActorComponent
    , public IPersistableComponent
{
    GENERATED_BODY()
public:

    // ── Configuration ────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, Category="Quest", meta=(ClampMin=1, ClampMax=100))
    int32 MaxActiveQuests = 20;

    // ── Replicated State ─────────────────────────────────────────────────────

    UPROPERTY(Replicated)
    FQuestRuntimeArray ActiveQuests;

    UPROPERTY(ReplicatedUsing=OnRep_CompletedQuestTags)
    FGameplayTagContainer CompletedQuestTags;

    UFUNCTION()
    void OnRep_CompletedQuestTags();

    // ── Delegates ─────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category="Quest")
    FOnQuestEventDelegate OnQuestEvent;

    UPROPERTY(BlueprintAssignable, Category="Quest")
    FOnTrackerUpdatedDelegate OnTrackerUpdated;

    // ── Public Server API ───────────────────────────────────────────────────

    // Called by external integration layers (game module bridge components).
    // The quest system has NO GMS subscriptions — this is the only entry point
    // for progress data. Clamps to EffectiveTarget. Fires TrackerUpdated GMS event.
    // After increment, immediately evaluates CompletionRequirements if tracker
    // reaches target. Virtual so USharedQuestComponent can route to coordinator.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    virtual void Server_IncrementTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    void Server_ForceCompleteQuest(const FGameplayTag& QuestId);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    void Server_ForceFailQuest(const FGameplayTag& QuestId);

    FQuestRuntime* FindActiveQuest(const FGameplayTag& QuestId);
    const FQuestRuntime* FindActiveQuest(const FGameplayTag& QuestId) const;

    // ── RPCs ───────────────────────────────────────────────────────────────────

    UFUNCTION(Server, Reliable)
    virtual void ServerRPC_AcceptQuest(FGameplayTag QuestId);

    UFUNCTION(Server, Reliable)
    void ServerRPC_AbandonQuest(FGameplayTag QuestId);

    UFUNCTION(Server, Reliable)
    void ServerRPC_RequestValidation(FGameplayTag QuestId, FGameplayTag StageTag);

    UFUNCTION(Client, Reliable)
    void ClientRPC_NotifyQuestEvent(FGameplayTag QuestId, EQuestEventType EventType);

    UFUNCTION(Client, Reliable)
    void ClientRPC_NotifyValidationRejected(
        FGameplayTag QuestId, FGameplayTag StageTag, EQuestRejectionReason Reason);

    // ── IPersistableComponent ─────────────────────────────────────────────────

    virtual FName  GetPersistenceKey()  const override { return TEXT("QuestComponent"); }
    virtual uint32 GetSchemaVersion()   const override { return 1; }
    virtual void   Serialize_Save(FArchive& Ar) override;
    virtual void   Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;

protected:
    TMap<FGameplayTag, FRequirementSetHandle> UnlockWatcherHandles;
    TMap<FGameplayTag, FRequirementSetHandle> CompletionWatcherHandles;

    UPROPERTY()
    TObjectPtr<URequirementWatcherComponent> WatcherComponent;

    virtual void Internal_CompleteQuest(FQuestRuntime& Runtime, const UQuestDefinition* Def);
    virtual void Internal_FailQuest(FQuestRuntime& Runtime, const UQuestDefinition* Def);
    void Internal_AdvanceStage(
        FQuestRuntime& Runtime, const UQuestDefinition* Def,
        const FGameplayTag& NewStageTag);
    void Internal_InitTrackers(
        FQuestRuntime& Runtime, const UQuestStageDefinition* StageDef,
        int32 GroupSize = 1);
    void Internal_RegisterCompletionWatcher(
        const FQuestRuntime& Runtime, const UQuestDefinition* Def);
    void Internal_RegisterUnlockWatcher(
        const FGameplayTag& QuestId, const UQuestDefinition* Def);

    FRequirementContext BuildRequirementContext(
        const FQuestRuntime& Runtime, const UQuestDefinition* Def) const;

    void ValidateActiveQuestsOnLogin();
    void RegisterUnlockWatchers();
    void RegisterClientValidatedWatchers();
};
```

---

## Accept Flow (ServerRPC_AcceptQuest)

```
ServerRPC_AcceptQuest(QuestId)
  1. Capacity check: ActiveQuests.Items.Num() >= MaxActiveQuests -> reject
  2. Duplicate check: already in ActiveQuests -> reject
  3. CompletedQuestTags pre-filter: tag present & Lifecycle is SingleAttempt -> reject
  4. UQuestRegistrySubsystem::GetOrLoadDefinitionAsync(QuestId)
  5. bEnabled check: Def->bEnabled == false -> reject
  6. Re-evaluate UnlockRequirements server-side
     BuildRequirementContext() with empty tracker payload
     -> Fail: ClientRPC_NotifyValidationRejected
  7. Create FQuestRuntime: QuestId, CurrentStageTag = StageGraph.EntryStateTag
  8. Internal_InitTrackers for entry stage (GroupSize = 1 for base component)
  9. ActiveQuests.Items.Add(Runtime), MarkArrayDirty
 10. NotifyDirty(this)
 11. Internal_RegisterCompletionWatcher for entry stage
 12. Broadcast GMS: GameCoreEvent.Quest.Started
 13. Broadcast GMS: GameCoreEvent.Quest.StageStarted
 14. ClientRPC_NotifyQuestEvent(QuestId, Started)
```

---

## Tracker Increment Flow (Server_IncrementTracker)

```
Server_IncrementTracker(QuestId, TrackerKey, Delta)
  1. Find FQuestRuntime* for QuestId -> not found: return
  2. Find FQuestTrackerEntry* for TrackerKey -> not found: return
  3. OldValue = Tracker.CurrentValue
  4. Tracker.CurrentValue = FMath::Min(Tracker.CurrentValue + Delta, Tracker.EffectiveTarget)
  5. If CurrentValue == OldValue: return (clamped, no-op)
  6. MarkItemDirty(Runtime) -> replication
  7. NotifyDirty(this)      -> persistence
  8. URequirementWatcherManager::NotifyPlayerEvent(
         PlayerState, RequirementEvent.Quest.TrackerUpdated)
  9. Broadcast GMS: GameCoreEvent.Quest.TrackerUpdated
 10. If Tracker.CurrentValue >= Tracker.EffectiveTarget:
       EvaluateCompletionRequirementsNow(QuestId)
```

---

## Watcher Registration and ContextBuilder

```cpp
void UQuestComponent::Internal_RegisterCompletionWatcher(
    const FQuestRuntime& Runtime, const UQuestDefinition* Def)
{
    const UQuestStageDefinition* Stage = Def->FindStage(Runtime.CurrentStageTag);
    if (!Stage || !Stage->CompletionRequirements) return;

    FGameplayTag QuestId = Runtime.QuestId;
    TWeakObjectPtr<UQuestComponent> WeakThis = this;

    FRequirementSetHandle Handle = WatcherComponent->RegisterSet(
        Stage->CompletionRequirements,
        FOnRequirementSetDirty::CreateUObject(
            this, &UQuestComponent::OnCompletionWatcherChanged),
        [WeakThis, QuestId](FRequirementContext& Ctx)
        {
            if (UQuestComponent* QC = WeakThis.Get())
            {
                if (const FQuestRuntime* QR = QC->FindActiveQuest(QuestId))
                {
                    FRequirementPayload Payload;
                    for (const FQuestTrackerEntry& T : QR->Trackers)
                        Payload.Counters.Add(T.TrackerKey, T.CurrentValue);
                    if (QR->LastCompletedTimestamp > 0)
                        Payload.Floats.Add(
                            FGameplayTag::RequestGameplayTag(
                                TEXT("Quest.Counter.LastCompleted")),
                            static_cast<float>(QR->LastCompletedTimestamp));
                    Ctx.PersistedData.Add(QuestId, MoveTemp(Payload));
                }
            }
        }
    );

    CompletionWatcherHandles.Add(QuestId, Handle);
}
```

---

## Validation Flow (ServerRPC_RequestValidation)

```
ServerRPC_RequestValidation(QuestId, StageTag)
  1. Find FQuestRuntime -> not found: silent reject
  2. Runtime.CurrentStageTag != StageTag -> ClientRPC_NotifyValidationRejected
  3. UQuestRegistrySubsystem::GetOrLoadDefinitionAsync(QuestId)
  4. bEnabled check -> reject if false
  5. Find UQuestStageDefinition for StageTag
  6. BuildRequirementContext() injecting tracker payload
  7. CompletionRequirements->EvaluateAsync(Context, callback):
     On Fail  -> ClientRPC_NotifyValidationRejected
     On Pass  ->
       bIsCompletionState -> Internal_CompleteQuest
       bIsFailureState    -> Internal_FailQuest
       else               -> evaluate StageGraph transitions -> Internal_AdvanceStage
```

---

## BeginPlay and Login Validation

```cpp
void UQuestComponent::BeginPlay()
{
    Super::BeginPlay();

    WatcherComponent =
        GetOwner()->FindComponentByClass<URequirementWatcherComponent>();
    checkf(WatcherComponent,
        TEXT("UQuestComponent requires URequirementWatcherComponent on APlayerState"));

    if (GetOwnerRole() == ROLE_Authority)
    {
        ValidateActiveQuestsOnLogin();

        for (const FQuestRuntime& Runtime : ActiveQuests.Items)
        {
            UQuestRegistrySubsystem* Registry =
                GetWorld()->GetSubsystem<UQuestRegistrySubsystem>();
            Registry->GetOrLoadDefinitionAsync(Runtime.QuestId,
                [this, &Runtime](const UQuestDefinition* Def)
                {
                    if (Def && Def->bEnabled)
                        Internal_RegisterCompletionWatcher(Runtime, Def);
                });
        }

        RegisterUnlockWatchers();
    }
    else if (IsOwner())
    {
        RegisterClientValidatedWatchers();
    }
}
```

---

## `USharedQuestComponent`

**File:** `Quest/Components/SharedQuestComponent.h / .cpp`

Inherits `UQuestComponent`. Adds group-aware accept flow and shared tracker routing via `USharedQuestCoordinator`. Uses `IGroupProvider` exclusively for group data access. When no `IGroupProvider` is present, behaves identically to the base component.

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API USharedQuestComponent : public UQuestComponent
{
    GENERATED_BODY()
public:

    // Applied by USharedQuestCoordinator when this player leaves a group mid-quest.
    // De-scaled tracker values are written directly into the active quest runtime.
    void Server_ApplyGroupSnapshot(const FQuestRuntime& SnapshotRuntime);

protected:
    // Overrides accept to handle USharedQuestDefinition::AcceptanceMode.
    // If LeaderAccept: routes to USharedQuestCoordinator::LeaderInitiateAccept.
    // If IndividualAccept or no IGroupProvider: calls base class accept directly.
    virtual void ServerRPC_AcceptQuest_Implementation(
        FGameplayTag QuestId) override;

    // Overrides tracker increment to route through USharedQuestCoordinator
    // when the quest is formally enrolled as a shared group quest.
    // Falls back to base class if no coordinator is active for this quest.
    virtual void Server_IncrementTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1) override;

private:
    IGroupProvider* GetGroupProvider() const
    {
        return Cast<IGroupProvider>(GetOwner());
    }

    // Weak reference to the coordinator on the group actor.
    // Null when no group is active or quest is not formally shared.
    TWeakObjectPtr<USharedQuestCoordinator> ActiveCoordinator;
};
```

### Accept Flow (USharedQuestComponent override)

```
ServerRPC_AcceptQuest_Implementation(QuestId)
  1. Load definition async (same as base)
  2. Upcast Def to USharedQuestDefinition* (may be null — valid)
  3. If SharedDef == null || AcceptanceMode == IndividualAccept:
       -> Call base ServerRPC_AcceptQuest (solo path, no group routing)
  4. If AcceptanceMode == LeaderAccept:
       IGroupProvider* Provider = GetGroupProvider()
       If Provider == null:
         -> Call base (no group available, treat as solo accept)
       If !Provider->IsGroupLeader():
         -> ClientRPC_NotifyValidationRejected(NotGroupLeader)
         // Only leader triggers LeaderAccept flow
       Else:
         -> Get group members: Provider->GetGroupMembers(Members)
         -> Find or create USharedQuestCoordinator on group actor
         -> Coordinator->LeaderInitiateAccept(QuestId, SharedDef, Members)
         // Coordinator fires GroupInvite event and calls OnRequestGroupEnrollment
         // Base accept runs for the leader immediately as Primary member
         -> Call base ServerRPC_AcceptQuest for self
```

> **Note:** Group size constraints are expressed as `URequirement_GroupSize` in `UnlockRequirements`. The base `ServerRPC_AcceptQuest` evaluates `UnlockRequirements` at step 6, which will reject if group size requirements are not met. `USharedQuestComponent` does not duplicate this check.
