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

UCLASS(ClassGroup=(PirateGame), meta=(BlueprintSpawnableComponent))
class PIRATEQUESTS_API UQuestComponent
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

    // Tags for permanently closed quests (complete or SingleAttempt fail).
    // Replicated — client pre-filters candidate sets using this.
    UPROPERTY(ReplicatedUsing=OnRep_CompletedQuestTags)
    FGameplayTagContainer CompletedQuestTags;

    UFUNCTION()
    void OnRep_CompletedQuestTags();

    // ── Delegates ─────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category="Quest")
    FOnQuestEventDelegate OnQuestEvent;

    UPROPERTY(BlueprintAssignable, Category="Quest")
    FOnTrackerUpdatedDelegate OnTrackerUpdated;

    // ── Public Server API ─────────────────────────────────────────────────────

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

    // Find an active quest runtime by ID. Returns nullptr if not active.
    FQuestRuntime* FindActiveQuest(const FGameplayTag& QuestId);
    const FQuestRuntime* FindActiveQuest(const FGameplayTag& QuestId) const;

    // ── RPCs ───────────────────────────────────────────────────────────────────

    UFUNCTION(Server, Reliable)
    virtual void ServerRPC_AcceptQuest(FGameplayTag QuestId);

    UFUNCTION(Server, Reliable)
    void ServerRPC_AbandonQuest(FGameplayTag QuestId);

    // ClientValidated path: client believes stage requirements are met.
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
    // Watcher handles for unlock requirement sets.
    // Keyed by QuestId. Unregistered when quest becomes Active or permanently closed.
    TMap<FGameplayTag, FRequirementSetHandle> UnlockWatcherHandles;

    // Watcher handles for stage completion requirement sets.
    // Keyed by QuestId. Replaced when stage changes.
    TMap<FGameplayTag, FRequirementSetHandle> CompletionWatcherHandles;

    UPROPERTY()
    TObjectPtr<URequirementWatcherComponent> WatcherComponent;

    // Internal flow — server-side only.
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

    // Builds FRequirementContext for a quest, injecting tracker payload.
    FRequirementContext BuildRequirementContext(
        const FQuestRuntime& Runtime, const UQuestDefinition* Def) const;

    // Called after persistence restore and on login.
    // Removes active quests whose definition has bEnabled=false.
    // Does NOT add QuestCompletedTag for removed quests.
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
  6. Re-evaluate UnlockRequirements server-side (always, regardless of CheckAuthority)
     BuildRequirementContext() with empty tracker payload (unlock reqs don't use trackers)
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
     -> watcher re-evaluates completion requirements for this quest
  9. Broadcast GMS: GameCoreEvent.Quest.TrackerUpdated
 10. If Tracker.CurrentValue >= Tracker.EffectiveTarget:
       EvaluateCompletionRequirementsNow(QuestId)
```

### Why Completion Is Evaluated on Increment, Not in the Watcher Flush

The watcher flush has a coalescing delay (default 0.5s). Evaluating completion immediately on tracker target-reach avoids this delay for the most time-sensitive player feedback (stage advance, quest complete). The watcher notification at step 8 still fires so that other systems watching the same tracker event (e.g. achievement trackers) get their coalesced update.

---

## Watcher Registration and ContextBuilder

Unlock watchers are registered with a `ContextBuilder` only if the unlock requirements contain `URequirement_Persisted` subclasses. For most unlock requirements (level, item, tag checks) the builder is null.

Completion watchers always use a `ContextBuilder` to inject the current tracker payload:

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
        // ContextBuilder: inject current tracker values as payload
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
  2. Runtime.CurrentStageTag != StageTag -> ClientRPC_NotifyValidationRejected(StageMismatch)
  3. UQuestRegistrySubsystem::GetOrLoadDefinitionAsync(QuestId)
  4. bEnabled check -> reject if false
  5. Find UQuestStageDefinition for StageTag
  6. BuildRequirementContext() injecting tracker payload
  7. CompletionRequirements->EvaluateAsync(Context, callback):
     On Fail  -> ClientRPC_NotifyValidationRejected(ConditionFailed)
     On Pass  ->
       bIsCompletionState -> Internal_CompleteQuest
       bIsFailureState    -> Internal_FailQuest
       else               -> evaluate StageGraph transitions for next state
                            -> Internal_AdvanceStage
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
        // 1. Remove any active quests whose definition is now disabled.
        //    Done before registering watchers to avoid wasting registrations.
        ValidateActiveQuestsOnLogin();

        // 2. Register completion watchers for all valid active quests.
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

        // 3. Register unlock watchers for candidate quests not yet active.
        RegisterUnlockWatchers();
    }
    else if (IsOwner())
    {
        // Owning client: register ClientValidated completion watchers for UI.
        RegisterClientValidatedWatchers();
    }
}

void UQuestComponent::ValidateActiveQuestsOnLogin()
{
    TArray<FGameplayTag> ToRemove;
    for (const FQuestRuntime& Runtime : ActiveQuests.Items)
    {
        // Synchronous check: if definition is not loaded yet, skip for now.
        // Async validation happens in the GetOrLoadDefinitionAsync callback above.
        const UQuestDefinition* Def =
            GetWorld()->GetSubsystem<UQuestRegistrySubsystem>()
                      ->GetDefinition(Runtime.QuestId);
        if (Def && !Def->bEnabled)
            ToRemove.Add(Runtime.QuestId);
    }
    for (const FGameplayTag& QuestId : ToRemove)
    {
        // Remove without adding to CompletedQuestTags — quest can be re-enabled.
        ActiveQuests.Items.RemoveAll(
            [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
        UE_LOG(LogQuest, Warning,
            TEXT("Removed disabled quest %s from active quests on login."),
            *QuestId.ToString());
    }
    if (ToRemove.Num() > 0)
    {
        ActiveQuests.MarkArrayDirty();
        NotifyDirty(this);
    }
}
```

---

## `USharedQuestComponent`

**File:** `Quest/Components/SharedQuestComponent.h / .cpp`

Inherits `UQuestComponent`. Adds group-aware accept validation, passive tracker fan-out, and coordinator integration. Uses `IGroupProvider` exclusively for all group data access.

```cpp
UCLASS(ClassGroup=(PirateGame), meta=(BlueprintSpawnableComponent))
class PIRATEQUESTS_API USharedQuestComponent : public UQuestComponent
{
    GENERATED_BODY()
public:

    // Applied snapshot from USharedQuestCoordinator when this player leaves a group.
    // De-scaled tracker values replace current active quest trackers.
    void Server_ApplyGroupSnapshot(const FQuestRuntime& SnapshotRuntime);

protected:
    // Overrides accept to add group size validation via IGroupProvider.
    // Also handles USharedQuestDefinition::AcceptanceMode (LeaderAccept flow).
    virtual void ServerRPC_AcceptQuest_Implementation(
        FGameplayTag QuestId) override;

    // Overrides tracker increment to also route through USharedQuestCoordinator
    // when the quest is a formally shared group quest.
    virtual void Server_IncrementTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1) override;

private:
    // Resolves IGroupProvider from the owning APlayerState.
    // Returns null if APlayerState does not implement IGroupProvider.
    // When null: component behaves identically to base UQuestComponent.
    IGroupProvider* GetGroupProvider() const
    {
        return Cast<IGroupProvider>(GetOwner());
    }

    // Weak reference to the coordinator on the group actor.
    // Set when a formally shared quest is accepted.
    // Null for solo quests and passive-contribution quests.
    TWeakObjectPtr<USharedQuestCoordinator> ActiveCoordinator;

    // Passive tracker contribution:
    // When a group member kills a mob, fans out to this player's active quests.
    // Called by the game module integration layer via the coordinator.
    void OnGroupMemberTrackerContribution(
        APlayerState* Contributor,
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta);
};
```

### Accept Flow Addition (USharedQuestComponent)

After step 4 (definition loaded) and before step 5 (bEnabled check), insert:

```
4a. Upcast Def to USharedQuestDefinition* (may be null — that is valid)
4b. If SharedDef != null && SharedDef->GroupRequirement != None:
      IGroupProvider* Provider = GetGroupProvider()
      if Provider == null -> reject (EQuestRejectionReason::GroupProviderMissing)
      GroupSize = Provider->GetGroupSize()
      if !SharedDef->IsGroupSizeValid(GroupSize) -> reject (GroupSizeMismatch)
4c. If SharedDef->AcceptanceMode == LeaderAccept && !Provider->IsGroupLeader():
      reject (NotGroupLeader)
      // Only the leader triggers LeaderAccept flow via the coordinator
```

### Passive Tracker Contribution (active solo quest in a group)

When a player has an active quest independently (not formally shared) and a group member triggers a relevant tracker event, `USharedQuestComponent` fans it out:

```
Game module bridge fires: USharedQuestComponent::OnGroupMemberTrackerContribution(
    Contributor, QuestId, TrackerKey, Delta)

  1. USharedQuestDefinition* SharedDef = Cast(GetRegistryDef(QuestId))
  2. if !SharedDef || !SharedDef->bAllowPassiveGroupContribution: return
  3. if FindActiveQuest(QuestId) == nullptr: return (player doesn't have this quest)
  4. Server_IncrementTracker(QuestId, TrackerKey, Delta)
     // Uses base class implementation since no coordinator is active for this quest
```

Note: The game module integration layer is responsible for calling this method. `USharedQuestComponent` does not subscribe to any GMS events.
