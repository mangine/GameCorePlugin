# UQuestComponent

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**File:** `Quest/Components/QuestComponent.h / .cpp` 
**Type:** `UActorComponent` on `APlayerState` 
**Implements:** `IPersistableComponent`

The per-player quest runtime. Owns all active quest state, drives watcher registration, handles accept/complete/fail flows, and persists to the serialization system.

---

## Class Declaration

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnQuestEventDelegate,
    FGameplayTag, QuestId,
    EQuestEventType, EventType);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnTrackerUpdatedDelegate,
    FGameplayTag, QuestId,
    FGameplayTag, TrackerKey,
    int32, NewValue);

UCLASS(ClassGroup=(PirateGame), meta=(BlueprintSpawnableComponent))
class PIRATEQUESTS_API UQuestComponent
    : public UActorComponent
    , public IPersistableComponent
{
    GENERATED_BODY()
public:

    // ── Configuration ────────────────────────────────────────────────────────

    // Maximum number of simultaneously active quests per player.
    // ServerRPC_AcceptQuest rejects when at capacity.
    // Includes party quests the player is enrolled in.
    UPROPERTY(EditDefaultsOnly, Category="Quest", meta=(ClampMin=1, ClampMax=100))
    int32 MaxActiveQuests = 20;

    // ── Replicated State ─────────────────────────────────────────────────────

    // Active quests. Delta replicated to owning client only.
    UPROPERTY(Replicated)
    FQuestRuntimeArray ActiveQuests;

    // Tags for permanently closed quests (completed or SingleAttempt failed).
    // Also includes cooldown-completed quests until they reset.
    // Replicated — client pre-filters candidate unlock sets using this.
    UPROPERTY(ReplicatedUsing=OnRep_CompletedQuestTags)
    FGameplayTagContainer CompletedQuestTags;

    UFUNCTION()
    void OnRep_CompletedQuestTags();

    // ── Delegates (Blueprint + C++) ─────────────────────────────────────────

    // Fires on client after any quest lifecycle event (started, completed, failed, etc.)
    UPROPERTY(BlueprintAssignable, Category="Quest")
    FOnQuestEventDelegate OnQuestEvent;

    // Fires on client when a tracker value changes.
    UPROPERTY(BlueprintAssignable, Category="Quest")
    FOnTrackerUpdatedDelegate OnTrackerUpdated;

    // ── Server API ─────────────────────────────────────────────────────────────

    // Increment a tracker server-side. Called by GMS event handlers.
    // Clamps to EffectiveTarget. Fires TrackerUpdated GMS event.
    // Party quests: also increments the coordinator's shared tracker.
    void Server_IncrementTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    // Force-complete a quest (e.g. admin command, scripted event).
    void Server_ForceCompleteQuest(const FGameplayTag& QuestId);

    // Force-fail a quest (e.g. timer expiry, dungeon wipe).
    void Server_ForceFailQuest(const FGameplayTag& QuestId);

    // Called by UPartyQuestCoordinator when the player leaves a party mid-quest.
    // Receives a de-scaled snapshot of tracker state.
    void Server_ApplyPartySnapshot(const FQuestRuntime& SnapshotRuntime);

    // ── RPCs ───────────────────────────────────────────────────────────────────

    // Client requests to accept a quest. Server validates, enrolls, starts trackers.
    UFUNCTION(Server, Reliable)
    void ServerRPC_AcceptQuest(FGameplayTag QuestId);

    // Client requests to abandon an active quest.
    UFUNCTION(Server, Reliable)
    void ServerRPC_AbandonQuest(FGameplayTag QuestId);

    // Client (on ClientValidated quests) signals it believes stage/quest requirements
    // are met. Server re-evaluates and confirms or rejects.
    UFUNCTION(Server, Reliable)
    void ServerRPC_RequestValidation(
        FGameplayTag QuestId,
        FGameplayTag StageTag);

    // Server notifies client of a quest lifecycle event.
    UFUNCTION(Client, Reliable)
    void ClientRPC_NotifyQuestEvent(
        FGameplayTag QuestId,
        EQuestEventType EventType);

    // Server notifies client that a validation request was rejected.
    UFUNCTION(Client, Reliable)
    void ClientRPC_NotifyValidationRejected(
        FGameplayTag QuestId,
        FGameplayTag StageTag,
        EQuestRejectionReason Reason);

    // ── IPersistableComponent ─────────────────────────────────────────────────

    virtual FName    GetPersistenceKey()  const override { return TEXT("QuestComponent"); }
    virtual uint32   GetSchemaVersion()   const override { return 1; }
    virtual void     Serialize_Save(FArchive& Ar) override;
    virtual void     Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;

private:
    // Watcher handles per active quest unlock set (for available quest detection).
    // Keyed by QuestId tag. Unregistered when quest becomes active or permanently closed.
    TMap<FGameplayTag, FRequirementSetHandle> UnlockWatcherHandles;

    // Watcher handles per active quest stage completion set.
    // Keyed by QuestId tag. Replaced when stage changes.
    TMap<FGameplayTag, FRequirementSetHandle> CompletionWatcherHandles;

    // The watcher component. Created and owned by this component's owner (APlayerState).
    UPROPERTY()
    TObjectPtr<URequirementWatcherComponent> WatcherComponent;

    // Internal flow methods (server-side only)
    void Internal_CompleteQuest(FQuestRuntime& Runtime, const UQuestDefinition* Def);
    void Internal_FailQuest(FQuestRuntime& Runtime, const UQuestDefinition* Def);
    void Internal_AdvanceStage(FQuestRuntime& Runtime,
                               const UQuestDefinition* Def,
                               const FGameplayTag& NewStageTag);
    void Internal_InitTrackers(FQuestRuntime& Runtime,
                               const UQuestStageDefinition* StageDef,
                               int32 PartySize);
    void Internal_RegisterCompletionWatcher(const FQuestRuntime& Runtime,
                                            const UQuestDefinition* Def);

    FRequirementContext BuildRequirementContext(const FQuestRuntime& Runtime,
                                               const UQuestDefinition* Def) const;
};
```

---

## Accept Flow (ServerRPC_AcceptQuest)

```
ServerRPC_AcceptQuest(QuestId)
  1. Capacity check: ActiveQuests.Items.Num() >= MaxActiveQuests -> reject
  2. Duplicate check: already in ActiveQuests -> reject
  3. CompletedQuestTags pre-filter: tag present & lifecycle is SingleAttempt -> reject
  4. UQuestRegistrySubsystem::GetOrLoadDefinition(QuestId) -> async if needed
  5. Group requirement check: validate party size against Def->GroupRequirement
  6. Re-evaluate UnlockRequirements server-side (always, regardless of CheckAuthority)
     BuildRequirementContext() with empty payload (unlock reqs don't need trackers)
     -> Fail: ClientRPC_NotifyValidationRejected
  7. Create FQuestRuntime, set QuestId, CurrentStageTag = StageGraph.EntryStateTag
  8. Internal_InitTrackers for entry stage
  9. ActiveQuests.Items.Add(Runtime), MarkArrayDirty
 10. RegisterCompletionWatcher for entry stage (if CheckAuthority == ServerAuthoritative)
     OR register on client watcher (if ClientValidated)
 11. NotifyDirty(this)
 12. Broadcast GMS: GameCoreEvent.Quest.Started
 13. Broadcast GMS: GameCoreEvent.Quest.StageStarted (entry stage)
 14. ClientRPC_NotifyQuestEvent(QuestId, EQuestEventType::Started)
```

---

## Validation Flow (ServerRPC_RequestValidation)

This is the `ClientValidated` path — client believes requirements are met and asks server to confirm.

```
ServerRPC_RequestValidation(QuestId, StageTag)
  1. Find FQuestRuntime for QuestId -> not found: silent reject (client state desync)
  2. Verify CurrentStageTag == StageTag -> mismatch: ClientRPC_NotifyValidationRejected
  3. UQuestRegistrySubsystem::GetOrLoadDefinition(QuestId)
  4. Find UQuestStageDefinition for StageTag
  5. BuildRequirementContext() injecting tracker payload
  6. CompletionRequirements->EvaluateAsync(Context, callback):
     On Fail  -> ClientRPC_NotifyValidationRejected(QuestId, StageTag, ConditionFailed)
     On Pass  ->
       Check if bIsCompletionState -> Internal_CompleteQuest
       Check if bIsFailureState    -> Internal_FailQuest
       Else                        -> evaluate StageGraph transitions,
                                     find next state, Internal_AdvanceStage
```

---

## Stage Advance Flow (Internal_AdvanceStage)

```
Internal_AdvanceStage(Runtime, Def, NewStageTag)
  1. Unregister old CompletionWatcherHandle for this quest
  2. Runtime.CurrentStageTag = NewStageTag
  3. Clear Runtime.Trackers
  4. Internal_InitTrackers(Runtime, Def->FindStage(NewStageTag), PartySize)
  5. MarkItemDirty(Runtime) -> replication
  6. NotifyDirty(this)      -> persistence
  7. Broadcast GMS: GameCoreEvent.Quest.StageCompleted (old stage)
  8. Broadcast GMS: GameCoreEvent.Quest.StageStarted (new stage)
  9. ClientRPC_NotifyQuestEvent(QuestId, StageStarted)
 10. Register new CompletionWatcher for new stage
```

---

## Complete / Fail Flows

```
Internal_CompleteQuest(Runtime, Def)
  1. Runtime.LastCompletedTimestamp = FDateTime::UtcNow().ToUnixTimestamp()
  2. Broadcast GMS: GameCoreEvent.Quest.Completed
     (includes QuestId, MemberRole, FirstTimeRewardTable/RepeatingRewardTable based on role)
  3. Unregister CompletionWatcherHandle
  4. if Lifecycle == SingleAttempt || Lifecycle == RetryUntilComplete:
       ActiveQuests.Items.Remove(Runtime)
       CompletedQuestTags.AddTag(Def->QuestCompletedTag)
     elif Lifecycle == RetryAndAssist || Lifecycle == Evergreen:
       ActiveQuests.Items.Remove(Runtime)
       // Re-register UnlockWatcher so quest becomes Available again
       // (after cadence/cooldown requirement clears)
  5. NotifyDirty(this)
  6. ClientRPC_NotifyQuestEvent(QuestId, Completed)

Internal_FailQuest(Runtime, Def)
  1. Broadcast GMS: GameCoreEvent.Quest.Failed
  2. Unregister CompletionWatcherHandle
  3. if Lifecycle == SingleAttempt:
       CompletedQuestTags.AddTag(Def->QuestCompletedTag) // permanently closed
       ActiveQuests.Items.Remove(Runtime)
  4. elif Lifecycle in {RetryUntilComplete, RetryAndAssist, Evergreen}:
       ActiveQuests.Items.Remove(Runtime)
       // Re-register UnlockWatcher (cooldown requirement blocks re-accept until ready)
  5. NotifyDirty(this)
  6. ClientRPC_NotifyQuestEvent(QuestId, Failed)
```

---

## Watcher Integration

`UQuestComponent` registers and unregisters requirement sets with `URequirementWatcherComponent` as quest state changes. The watcher fires `OnSetChanged` when requirements transition, which the component uses to trigger the appropriate flow.

```cpp
// On ServerAuthoritative quests:
// Watcher runs server-side. On completion requirements pass:
void UQuestComponent::OnCompletionWatcherChanged(
    FRequirementSetHandle Handle, bool bAllPassed)
{
    if (!bAllPassed) return;

    // Find which quest this handle belongs to
    const FGameplayTag* QuestId = CompletionWatcherHandles.FindKey(Handle);
    if (!QuestId) return;

    FQuestRuntime* Runtime = FindActiveQuest(*QuestId);
    if (!Runtime) return;

    ServerRPC_RequestValidation(*QuestId, Runtime->CurrentStageTag);
    // This re-path through RequestValidation ensures the same
    // server-authoritative evaluation runs for both paths.
}

// On ClientValidated quests:
// Watcher runs on owning client. On pass:
// -> fires ServerRPC_RequestValidation from client.
```

---

## BeginPlay and Post-Load Watcher Registration

```cpp
void UQuestComponent::BeginPlay()
{
    Super::BeginPlay();

    // Find or create WatcherComponent on owning PlayerState
    WatcherComponent = GetOwner()->FindComponentByClass<URequirementWatcherComponent>();
    checkf(WatcherComponent,
        TEXT("UQuestComponent requires URequirementWatcherComponent on APlayerState"));

    if (GetOwnerRole() == ROLE_Authority)
    {
        // Register unlock watchers for all quests NOT in CompletedQuestTags
        // and NOT already active. Done after persistence load.
        // UQuestRegistrySubsystem provides the candidate list.
        RegisterUnlockWatchers();

        // Register completion watchers for all currently active quests
        for (const FQuestRuntime& Runtime : ActiveQuests.Items)
        {
            UQuestRegistrySubsystem* Registry =
                GetWorld()->GetSubsystem<UQuestRegistrySubsystem>();
            Registry->GetOrLoadDefinitionAsync(Runtime.QuestId,
                [this, &Runtime](const UQuestDefinition* Def)
                {
                    if (Def)
                        Internal_RegisterCompletionWatcher(Runtime, Def);
                });
        }
    }
    else if (IsOwner()) // owning client
    {
        // Register client-side watchers for ClientValidated active quests
        RegisterClientValidatedWatchers();
    }
}
```

---

## Tracker Increment (Server)

Trackers are incremented by subscribing to GMS events. The quest component subscribes at `BeginPlay` to relevant event channels.

```cpp
// Example: subscribing to mob kill events to drive kill trackers
void UQuestComponent::BeginPlay()
{
    // ... (other setup)

    if (HasAuthority())
    {
        auto& GMS = UGameCoreEventSubsystem::Get(this);
        GMS.RegisterListener(
            FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Combat.MobKilled")),
            this,
            &UQuestComponent::OnMobKilled);
    }
}

void UQuestComponent::OnMobKilled(const FMobKilledEventPayload& Payload)
{
    // Find all active quests in the kill stage that have a kill tracker
    // whose filter tag matches the killed mob type.
    for (FQuestRuntime& Runtime : ActiveQuests.Items)
    {
        for (FQuestTrackerEntry& Tracker : Runtime.Trackers)
        {
            // Tracker key convention: Quest.Counter.KillCount
            // Additional filter matching (mob type tag) is responsibility of
            // the quest definition via a URequirement_KillCount subclass.
            if (Tracker.TrackerKey.MatchesTag(
                    FGameplayTag::RequestGameplayTag(TEXT("Quest.Counter.Kill"))))
            {
                Server_IncrementTracker(
                    Runtime.QuestId, Tracker.TrackerKey, 1);
            }
        }
    }
}
```

> **Important:** Tracker increment logic should be as lightweight as possible. Tag matching on the hot combat path must not involve asset loads. The `UQuestStageDefinition::Trackers` array is already loaded since the definition is in memory for all active quests.

---

## Supporting Enums

**File:** `Quest/Enums/QuestEnums.h` (add to existing)

```cpp
UENUM(BlueprintType)
enum class EQuestEventType : uint8
{
    Started,
    Completed,
    Failed,
    Abandoned,
    StageStarted,
    StageCompleted,
    StageFailed,
    BecameAvailable,
    TrackerUpdated,
};

UENUM(BlueprintType)
enum class EQuestRejectionReason : uint8
{
    AtCapacity,
    AlreadyActive,
    PermanentlyClosed,
    GroupSizeMismatch,
    RequirementsNotMet,
    StageTagMismatch,
    DefinitionNotFound,
};
```
