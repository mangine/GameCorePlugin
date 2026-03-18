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

    // Quest system configuration asset. Loaded at BeginPlay.
    // Controls MaxActiveQuests and other tunables without recompiling.
    // If null, component uses built-in defaults.
    UPROPERTY(EditDefaultsOnly, Category="Quest")
    TSoftObjectPtr<UQuestConfigDataAsset> QuestConfig;

    // ── Replicated State ─────────────────────────────────────────────────────

    UPROPERTY(Replicated)
    FQuestRuntimeArray ActiveQuests;

    // Tags for permanently closed quests (complete or SingleAttempt fail).
    // Replicated — client pre-filters candidate sets and drives UI.
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

    // Entry point for all tracker progress. Called by external integration layers.
    // The quest system has NO GMS subscriptions — this is the only way progress
    // enters the system. Virtual so USharedQuestComponent can route through
    // USharedQuestCoordinator for formally shared group quests.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    virtual void Server_IncrementTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    void Server_ForceCompleteQuest(const FGameplayTag& QuestId);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    void Server_ForceFailQuest(const FGameplayTag& QuestId);

    // Removes CompletedQuestTag for quests matching the given cadence,
    // and re-registers their unlock watchers. Called by UQuestRegistrySubsystem
    // on Daily/Weekly reset. Never adds to ActiveQuests — player must accept again.
    void FlushCadenceResets(EQuestResetCadence Cadence);

    FQuestRuntime*       FindActiveQuest(const FGameplayTag& QuestId);
    const FQuestRuntime* FindActiveQuest(const FGameplayTag& QuestId) const;

    // Returns the resolved MaxActiveQuests from QuestConfig, or default 20.
    int32 GetMaxActiveQuests() const;

    // ── RPCs ───────────────────────────────────────────────────────────────────

    UFUNCTION(Server, Reliable)
    virtual void ServerRPC_AcceptQuest(FGameplayTag QuestId);

    UFUNCTION(Server, Reliable)
    void ServerRPC_AbandonQuest(FGameplayTag QuestId);

    // ClientValidated path: client watcher passed, requesting server validation.
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
    virtual void   Serialize_Save(FArchive& Ar) override; // see Runtime Structs.md
    virtual void   Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void   Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion) override;

protected:
    // Watcher handles for unlock requirement sets. Keyed by QuestId.
    // Unregistered when quest becomes Active or permanently closed.
    TMap<FGameplayTag, FRequirementSetHandle> UnlockWatcherHandles;

    // Watcher handles for stage completion requirement sets. Keyed by QuestId.
    // Replaced on stage advance.
    TMap<FGameplayTag, FRequirementSetHandle> CompletionWatcherHandles;

    UPROPERTY()
    TObjectPtr<URequirementWatcherComponent> WatcherComponent;

    // Loaded synchronously at BeginPlay from QuestConfig soft reference.
    UPROPERTY()
    TObjectPtr<UQuestConfigDataAsset> LoadedConfig;

    // ── Internal flow (server-side only) ─────────────────────────────────────

    virtual void Internal_CompleteQuest(FQuestRuntime& Runtime,
                                        const UQuestDefinition* Def);
    virtual void Internal_FailQuest(FQuestRuntime& Runtime,
                                    const UQuestDefinition* Def);
    void Internal_AdvanceStage(FQuestRuntime& Runtime,
                               const UQuestDefinition* Def,
                               const FGameplayTag& NewStageTag);
    void Internal_InitTrackers(FQuestRuntime& Runtime,
                               const UQuestStageDefinition* StageDef,
                               int32 GroupSize = 1);
    void Internal_RegisterCompletionWatcher(const FQuestRuntime& Runtime,
                                            const UQuestDefinition* Def);
    void Internal_RegisterUnlockWatcher(const FGameplayTag& QuestId,
                                        const UQuestDefinition* Def);
    void EvaluateCompletionRequirementsNow(const FGameplayTag& QuestId);

    // Evaluates StageGraph transitions from the current stage using
    // UStateMachineAsset::FindFirstPassingTransition and returns the next
    // stage tag. Returns an invalid tag if no transition passes.
    FGameplayTag ResolveNextStage(const FQuestRuntime& Runtime,
                                  const UQuestDefinition* Def) const;

    FRequirementContext BuildRequirementContext(const FQuestRuntime& Runtime,
                                               const UQuestDefinition* Def) const;

    // Called first in BeginPlay (server). Removes active quests whose
    // definition is disabled. Non-destructive: no QuestCompletedTag added.
    // Runs async for definitions not yet loaded, synchronously for cached ones.
    void ValidateActiveQuestsOnLogin();

    void RegisterUnlockWatchers();
    void RegisterClientValidatedWatchers();
};
```

---

## `UQuestConfigDataAsset`

**File:** `Quest/Data/QuestConfigDataAsset.h`

```cpp
// Project-level quest system configuration. Referenced softly by UQuestComponent.
// Change values here without touching component defaults or recompiling.
UCLASS(BlueprintType)
class GAMECORE_API UQuestConfigDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    // Maximum simultaneously active quests per player.
    // Enforced server-side at ServerRPC_AcceptQuest.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(ClampMin=1, ClampMax=200))
    int32 MaxActiveQuests = 20;

    // Future tunables go here: MaxDailyQuestSlots, MaxSharedQuestSlots, etc.
};
```

---

## Accept Flow (ServerRPC_AcceptQuest)

```
ServerRPC_AcceptQuest(QuestId)
  1. Capacity check: ActiveQuests.Items.Num() >= GetMaxActiveQuests() -> reject
  2. Duplicate check: already in ActiveQuests -> reject
  3. CompletedQuestTags pre-filter: tag present & Lifecycle is SingleAttempt -> reject
  4. UQuestRegistrySubsystem::GetOrLoadDefinitionAsync(QuestId, callback)
     [callback may fire synchronously if already loaded, or async on first load]
  IN CALLBACK:
  5. bEnabled check: Def->bEnabled == false -> reject
  6. Re-evaluate UnlockRequirements server-side (always, regardless of CheckAuthority)
     BuildRequirementContext() with empty tracker payload
     -> Fail: ClientRPC_NotifyValidationRejected
  7. Create FQuestRuntime: QuestId, CurrentStageTag = StageGraph.EntryStateTag
  8. Internal_InitTrackers for entry stage (GroupSize = 1 for base component)
  9. ActiveQuests.Items.Add(Runtime), MarkArrayDirty
 10. UQuestRegistrySubsystem::AddReference(QuestId)
 11. NotifyDirty(this)
 12. Internal_RegisterCompletionWatcher for entry stage
 13. Unregister UnlockWatcherHandle for this QuestId (no longer a candidate)
 14. Broadcast GMS: GameCoreEvent.Quest.Started
 15. Broadcast GMS: GameCoreEvent.Quest.StageStarted
 16. ClientRPC_NotifyQuestEvent(QuestId, Started)
```

---

## Tracker Increment Flow (Server_IncrementTracker)

This is the **only entry point** for progress data. No GMS subscriptions inside this component.

```
Server_IncrementTracker(QuestId, TrackerKey, Delta)
  1. Find FQuestRuntime* for QuestId -> not found: return
  2. Find FQuestTrackerEntry* for TrackerKey -> not found: return
  3. OldValue = Tracker.CurrentValue
  4. Tracker.CurrentValue = FMath::Min(Tracker.CurrentValue + Delta,
                                       Tracker.EffectiveTarget)
  5. If CurrentValue == OldValue: return (clamped, no-op)
  6. MarkItemDirty(Runtime) -> replication
  7. NotifyDirty(this)      -> persistence
  8. URequirementWatcherManager::NotifyPlayerEvent(
         PlayerState, RequirementEvent.Quest.TrackerUpdated)
     // Notifies watcher for any sets watching this tag (achievements, etc.)
     // Does NOT drive completion — that is done in step 9.
  9. Broadcast GMS: GameCoreEvent.Quest.TrackerUpdated
 10. If Tracker.CurrentValue >= Tracker.EffectiveTarget:
       EvaluateCompletionRequirementsNow(QuestId)
       // Immediate, imperative — no watcher flush delay.
```

> **Single completion path.** Completion is evaluated imperatively at step 10 only. The watcher notification at step 8 serves other systems (achievements, UI progress bars) — it does not trigger another completion check. There is no completion watcher `OnDirty` callback that drives `Internal_CompleteQuest` / `Internal_FailQuest`. This avoids double-evaluation.

---

## Stage Transition Logic (ResolveNextStage)

When `CompletionRequirements` pass for the current stage and the stage is neither a completion nor failure terminal, `UQuestComponent` resolves the next stage by evaluating the `UStateMachineAsset` transition graph directly.

```cpp
FGameplayTag UQuestComponent::ResolveNextStage(
    const FQuestRuntime& Runtime,
    const UQuestDefinition* Def) const
{
    FRequirementContext Ctx = BuildRequirementContext(Runtime, Def);

    // FindFirstPassingTransition evaluates UQuestTransitionRule instances
    // against the FRequirementContext passed as ContextObject.
    // The asset tries AnyState transitions first, then FromState transitions.
    return Def->StageGraph->FindFirstPassingTransition(
        Runtime.CurrentStageTag,
        &Ctx); // passed as UObject* ContextObject
}
```

`UQuestTransitionRule::Evaluate` receives `&Ctx` as its `ContextObject` and casts it to `FRequirementContext*`. It then calls `Requirements->Evaluate(Ctx)` and returns the result. Designers author branching stage graphs by adding multiple `UQuestTransitionRule`-based transitions from the same source state, each with different requirement conditions. The first passing transition wins.

---

## Validation Flow (ServerRPC_RequestValidation)

Used by the `ClientValidated` path when the client watcher detects completion requirements passing.

```
ServerRPC_RequestValidation(QuestId, StageTag)
  1. Find FQuestRuntime -> not found: silent reject
  2. Runtime.CurrentStageTag != StageTag -> ClientRPC_NotifyValidationRejected(StageMismatch)
  3. UQuestRegistrySubsystem::GetOrLoadDefinitionAsync(QuestId, callback)
  IN CALLBACK:
  4. bEnabled check -> reject if false
  5. Find UQuestStageDefinition for StageTag
  6. BuildRequirementContext() injecting tracker payload
  7. CompletionRequirements->EvaluateAsync(Context, callback):
     On Fail  -> ClientRPC_NotifyValidationRejected(ConditionFailed)
     On Pass  ->
       Check UQuestStateNode flags on current stage:
         bIsCompletionState -> Internal_CompleteQuest
         bIsFailureState    -> Internal_FailQuest
       else:
         ResolveNextStage() -> Internal_AdvanceStage(NextStageTag)
         If no passing transition: log warning, no advance
```

---

## Complete / Fail Flows

```
Internal_CompleteQuest(Runtime, Def)
  1. Runtime.LastCompletedTimestamp = FDateTime::UtcNow().ToUnixTimestamp()
  2. Unregister CompletionWatcherHandle for this quest
  3. UQuestRegistrySubsystem::ReleaseReference(QuestId)
  4. Switch on Lifecycle:
     SingleAttempt | RetryUntilComplete:
       Remove from ActiveQuests
       CompletedQuestTags.AddTag(Def->QuestCompletedTag)
     RetryAndAssist | Evergreen:
       Remove from ActiveQuests
       Re-register UnlockWatcher (cooldown requirement blocks re-accept until ready)
  5. NotifyDirty(this)
  6. Broadcast GMS: GameCoreEvent.Quest.Completed
     (payload includes FirstTimeRewardTable or RepeatingRewardTable
      based on whether this is first completion — tracked via CompletedQuestTags)
  7. ClientRPC_NotifyQuestEvent(QuestId, Completed)

Internal_FailQuest(Runtime, Def)
  1. Unregister CompletionWatcherHandle
  2. UQuestRegistrySubsystem::ReleaseReference(QuestId)
  3. Switch on Lifecycle:
     SingleAttempt:
       CompletedQuestTags.AddTag(Def->QuestCompletedTag) // permanently closed
       Remove from ActiveQuests
     RetryUntilComplete | RetryAndAssist | Evergreen:
       Remove from ActiveQuests
       Re-register UnlockWatcher
  4. NotifyDirty(this)
  5. Broadcast GMS: GameCoreEvent.Quest.Failed
  6. ClientRPC_NotifyQuestEvent(QuestId, Failed)
```

---

## BeginPlay and Login Validation

The ordering here is critical. Watcher registration must not happen until disabled quests are removed.

```cpp
void UQuestComponent::BeginPlay()
{
    Super::BeginPlay();

    // Load config synchronously (forced load — small asset, always in memory).
    if (!QuestConfig.IsNull())
        LoadedConfig = QuestConfig.LoadSynchronous();

    WatcherComponent =
        GetOwner()->FindComponentByClass<URequirementWatcherComponent>();
    checkf(WatcherComponent,
        TEXT("UQuestComponent requires URequirementWatcherComponent on APlayerState"));

    if (GetOwnerRole() == ROLE_Authority)
    {
        // Step 1: async-validate all active quests against bEnabled.
        // Watcher registration is deferred until all validations complete.
        // Uses a shared counter to know when all async loads are done.
        ValidateActiveQuestsOnLogin();
        // ValidateActiveQuestsOnLogin calls RegisterUnlockWatchers() and
        // completion watcher registration as its final step after all
        // definition loads resolve.
    }
    else if (IsOwner())
    {
        // Owning client: register ClientValidated completion watchers for UI.
        RegisterClientValidatedWatchers();
    }
}
```

### ValidateActiveQuestsOnLogin — Ordered Async Pattern

```cpp
void UQuestComponent::ValidateActiveQuestsOnLogin()
{
    // Collect all QuestIds that need validation.
    TArray<FGameplayTag> QuestIds;
    for (const FQuestRuntime& R : ActiveQuests.Items)
        QuestIds.Add(R.QuestId);

    if (QuestIds.IsEmpty())
    {
        // No active quests — go straight to watcher registration.
        RegisterUnlockWatchers();
        return;
    }

    // Use a shared counter: when all loads complete, proceed to watcher registration.
    TSharedRef<int32> Pending = MakeShared<int32>(QuestIds.Num());
    TWeakObjectPtr<UQuestComponent> WeakThis = this;

    auto OnAllLoaded = [WeakThis]()
    {
        if (UQuestComponent* QC = WeakThis.Get())
            QC->RegisterUnlockWatchers();
    };

    UQuestRegistrySubsystem* Registry =
        GetGameInstance()->GetSubsystem<UQuestRegistrySubsystem>();

    for (const FGameplayTag& QuestId : QuestIds)
    {
        Registry->GetOrLoadDefinitionAsync(QuestId,
            [this, QuestId, Pending, OnAllLoaded](const UQuestDefinition* Def)
            {
                if (Def && !Def->bEnabled)
                {
                    ActiveQuests.Items.RemoveAll(
                        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
                    // No QuestCompletedTag — non-destructive removal.
                    UE_LOG(LogQuest, Warning,
                        TEXT("Removed disabled quest '%s' on login."),
                        *QuestId.ToString());
                }
                else if (Def && Def->bEnabled)
                {
                    Internal_RegisterCompletionWatcher(
                        *FindActiveQuest(QuestId), Def);
                }

                --(*Pending);
                if (*Pending == 0)
                {
                    if (ActiveQuests.Items.Num() > 0)
                        ActiveQuests.MarkArrayDirty();
                    OnAllLoaded();
                }
            });
    }
}
```

---

## Unlock Authority — Two Paths

```
ServerAuthoritative quests:
  Server watcher fires OnDirty (all-pass)
    -> Server evaluates UnlockRequirements from authoritative context
    -> Server fires ClientRPC_NotifyQuestEvent(BecameAvailable)
    -> Client shows "Quest Available" UI
    -> Player interacts with NPC / quest giver
    -> Client fires ServerRPC_AcceptQuest
    -> Server validates UnlockRequirements again before accepting

ClientValidated quests:
  Client watcher fires OnDirty (all-pass) [using replicated FQuestRuntime data]
    -> Client shows "Quest Available" UI immediately (responsive)
    -> Client fires ServerRPC_AcceptQuest
    -> Server validates UnlockRequirements fully before accepting
    -> If server rejects: ClientRPC_NotifyValidationRejected -> UI reverts
```

> The server **always** re-evaluates `UnlockRequirements` at `ServerRPC_AcceptQuest` step 6, regardless of authority mode. `CheckAuthority` controls when the client receives UI feedback, not whether the server validates.

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
            this, &UQuestComponent::OnUnlockWatcherChanged),
        // ContextBuilder: inject current tracker values as payload
        [WeakThis, QuestId](FRequirementContext& Ctx)
        {
            if (UQuestComponent* QC = WeakThis.Get())
            {
                Ctx.QuestComponent = QC; // cached pointer for requirement use
                if (const FQuestRuntime* QR = QC->FindActiveQuest(QuestId))
                {
                    FRequirementPayload Payload;
                    for (const FQuestTrackerEntry& T : QR->Trackers)
                        Payload.Counters.Add(T.TrackerKey, T.CurrentValue);
                    Ctx.PersistedData.Add(QuestId, MoveTemp(Payload));
                }
            }
        }
    );
    CompletionWatcherHandles.Add(QuestId, Handle);
}
```

> **Completion watcher purpose.** This watcher notifies external systems (achievements, analytics) via `RequirementEvent.Quest.TrackerUpdated`. It does NOT drive `Internal_CompleteQuest`. Completion is triggered only from `EvaluateCompletionRequirementsNow` called by `Server_IncrementTracker`. The `OnDirty` callback here should notify the UI or achievements, not attempt a stage advance.

---

## `USharedQuestComponent`

**File:** `Quest/Components/SharedQuestComponent.h / .cpp`

Inherits `UQuestComponent`. Adds group-aware accept flow and shared tracker routing. Uses `IGroupProvider` for all group data access. Falls back to base behavior when no `IGroupProvider` is present.

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API USharedQuestComponent : public UQuestComponent
{
    GENERATED_BODY()
public:
    // Applied by USharedQuestCoordinator when this player leaves a group mid-quest.
    void Server_ApplyGroupSnapshot(const FQuestRuntime& SnapshotRuntime);

protected:
    virtual void ServerRPC_AcceptQuest_Implementation(
        FGameplayTag QuestId) override;

    virtual void Server_IncrementTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1) override;

private:
    IGroupProvider* GetGroupProvider() const
    {
        return Cast<IGroupProvider>(GetOwner());
    }

    // Locates the USharedQuestCoordinator via IGroupProvider::GetGroupActor().
    // Returns null if not in a group or group actor has no coordinator.
    USharedQuestCoordinator* GetCoordinator() const
    {
        IGroupProvider* Provider = GetGroupProvider();
        if (!Provider) return nullptr;
        AActor* GroupActor = Provider->GetGroupActor();
        if (!GroupActor) return nullptr;
        return GroupActor->FindComponentByClass<USharedQuestCoordinator>();
    }

    TWeakObjectPtr<USharedQuestCoordinator> CachedCoordinator;
};
```

### Accept Flow (USharedQuestComponent override)

```
ServerRPC_AcceptQuest_Implementation(QuestId)
  1. Load definition async (same as base)
  2. Upcast Def to USharedQuestDefinition* (may be null — valid, treat as solo)
  3. If SharedDef == null || AcceptanceMode == IndividualAccept:
       -> Call base ServerRPC_AcceptQuest (solo path)
  4. If AcceptanceMode == LeaderAccept:
       Provider = GetGroupProvider()
       If Provider == null -> call base (no group, treat as solo)
       If !Provider->IsGroupLeader() -> ClientRPC_NotifyValidationRejected(NotGroupLeader)
       Else:
         Provider->GetGroupMembers(Members) // output param
         Remove self from Members
         Coordinator = GetCoordinator()
         If Coordinator == null -> call base (no coordinator, treat as solo)
         Coordinator->LeaderInitiateAccept(QuestId, SharedDef, Members)
         Call base ServerRPC_AcceptQuest for self (leader enrolled as Primary)
```

> **Group size constraints** are expressed as `URequirement_GroupSize` in `UnlockRequirements`. Base `ServerRPC_AcceptQuest` evaluates these at step 6. `USharedQuestComponent` does not duplicate this check.
