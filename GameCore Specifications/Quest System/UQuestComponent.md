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
    // No GMS subscriptions inside this component — this is the only way
    // progress enters the system.
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
    // re-registers their unlock watchers. Called by UQuestRegistrySubsystem
    // on Daily/Weekly reset.
    void FlushCadenceResets(EQuestResetCadence Cadence);

    FQuestRuntime*       FindActiveQuest(const FGameplayTag& QuestId);
    const FQuestRuntime* FindActiveQuest(const FGameplayTag& QuestId) const;

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
    virtual void   Serialize_Save(FArchive& Ar) override;
    virtual void   Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void   Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion) override;

protected:
    // Unlock watcher handles keyed by QuestId.
    // Each handle is returned by URequirementList::RegisterWatch.
    // Unregistered when the quest becomes Active or permanently closed.
    TMap<FGameplayTag, FEventWatchHandle> UnlockWatcherHandles;

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

    // Evaluates completion requirements immediately (imperative).
    // Called by Server_IncrementTracker when a tracker reaches its target.
    void EvaluateCompletionRequirementsNow(const FGameplayTag& QuestId);

    FGameplayTag ResolveNextStage(const FQuestRuntime& Runtime,
                                  const UQuestDefinition* Def) const;

    // Builds a FRequirementContext containing a FPlayerContext snapshot.
    // Quest requirements that need component data use
    // PlayerState->FindComponentByClass<T>() inside their own Evaluate override.
    FRequirementContext BuildRequirementContext() const;

    // Step 1 of BeginPlay (server): remove disabled active quests async,
    // then call RegisterUnlockWatchers as final step.
    void ValidateActiveQuestsOnLogin();

    // Step 2 of BeginPlay (server): register unlock watchers for all
    // candidate quests. Performs an immediate imperative baseline check
    // for each list before handing off to the reactive watcher.
    void RegisterUnlockWatchers();

    // Called on owning client in BeginPlay. Registers ClientValidated
    // completion watchers so the client can fire ServerRPC_RequestValidation
    // when stage requirements pass.
    void RegisterClientValidatedCompletionWatchers();

    // Returns true if this quest should have an unlock watcher registered.
    // Checks: not active, not permanently closed, bEnabled, lifecycle allows attempt.
    bool ShouldWatchUnlock(const FGameplayTag& QuestId,
                           const UQuestDefinition* Def) const;
};
```

---

## Which Quests Get Unlock Watchers

Only quests that are genuinely candidates for unlocking need a watcher. `ShouldWatchUnlock` enforces:

```cpp
bool UQuestComponent::ShouldWatchUnlock(
    const FGameplayTag& QuestId, const UQuestDefinition* Def) const
{
    if (!Def || !Def->bEnabled) return false;

    // Already active — no need to watch.
    if (FindActiveQuest(QuestId) != nullptr) return false;

    // Permanently closed for this player?
    // SingleAttempt and RetryUntilComplete quests with a QuestCompletedTag
    // cannot be re-attempted.
    switch (Def->Lifecycle)
    {
    case EQuestLifecycle::SingleAttempt:
    case EQuestLifecycle::RetryUntilComplete:
        if (CompletedQuestTags.HasTag(Def->QuestCompletedTag))
            return false;
        break;
    case EQuestLifecycle::RetryAndAssist:
    case EQuestLifecycle::Evergreen:
        // Always re-watchable after cooldown — cooldown is expressed
        // as URequirement_QuestCooldown in UnlockRequirements.
        break;
    }

    return true;
}
```

---

## Accept Flow (ServerRPC_AcceptQuest)

```
ServerRPC_AcceptQuest(QuestId)
  1. Capacity check: ActiveQuests.Items.Num() >= GetMaxActiveQuests() -> reject
  2. Duplicate check: already in ActiveQuests -> reject
  3. CompletedQuestTags pre-filter: tag present & Lifecycle is SingleAttempt -> reject
  4. UQuestRegistrySubsystem::GetOrLoadDefinitionAsync(QuestId, callback)
  IN CALLBACK:
  5. bEnabled check -> reject if false
  6. Re-evaluate UnlockRequirements server-side (always).
     BuildRequirementContext() -> List->Evaluate(Ctx)
     -> Fail: ClientRPC_NotifyValidationRejected
  7. Create FQuestRuntime: QuestId, CurrentStageTag = StageGraph.EntryStateTag
  8. Internal_InitTrackers for entry stage
  9. ActiveQuests.Items.Add(Runtime), MarkArrayDirty
 10. UQuestRegistrySubsystem::AddReference(QuestId)
 11. NotifyDirty(this)
 12. Unregister UnlockWatcherHandle for this QuestId
 13. Broadcast GMS: GameCoreEvent.Quest.Started
 14. Broadcast GMS: GameCoreEvent.Quest.StageStarted
 15. ClientRPC_NotifyQuestEvent(QuestId, Started)
```

---

## Tracker Increment Flow (Server_IncrementTracker)

```
Server_IncrementTracker(QuestId, TrackerKey, Delta)
  1. Find FQuestRuntime* -> not found: return
  2. Find FQuestTrackerEntry* -> not found: return
  3. OldValue = Tracker.CurrentValue
  4. Tracker.CurrentValue = FMath::Min(CurrentValue + Delta, EffectiveTarget)
  5. If CurrentValue == OldValue: return (clamped, no-op)
  6. MarkItemDirty(Runtime) -> replication
  7. NotifyDirty(this)      -> persistence
  8. Broadcast GMS: GameCoreEvent.Quest.TrackerUpdated
     (external systems — achievements, UI — listen here)
  9. If Tracker.CurrentValue >= Tracker.EffectiveTarget:
       EvaluateCompletionRequirementsNow(QuestId)  <- imperative, immediate
```

> **Single completion path.** Completion is always driven imperatively from `Server_IncrementTracker` step 9. There is no reactive completion watcher — the watcher system is used exclusively for unlock detection.

---

## `EvaluateCompletionRequirementsNow`

```cpp
void UQuestComponent::EvaluateCompletionRequirementsNow(const FGameplayTag& QuestId)
{
    const FQuestRuntime* Runtime = FindActiveQuest(QuestId);
    if (!Runtime) return;

    // Load definition — must already be resident (called after tracker increment
    // which requires the quest to be active, which requires the definition
    // to have been loaded at accept time).
    UQuestRegistrySubsystem* Registry =
        GetGameInstance()->GetSubsystem<UQuestRegistrySubsystem>();
    const UQuestDefinition* Def = Registry->FindLoadedDefinition(QuestId);
    if (!Def) return;

    const UQuestStageDefinition* Stage = Def->FindStage(Runtime->CurrentStageTag);
    if (!Stage || !Stage->CompletionRequirements) return;

    FRequirementContext Ctx = BuildRequirementContext();
    FRequirementResult Result = Stage->CompletionRequirements->Evaluate(Ctx);
    if (!Result.bPassed) return;

    if (Stage->bIsCompletionState)      Internal_CompleteQuest(*FindActiveQuest(QuestId), Def);
    else if (Stage->bIsFailureState)    Internal_FailQuest(*FindActiveQuest(QuestId), Def);
    else
    {
        FGameplayTag Next = ResolveNextStage(*Runtime, Def);
        if (Next.IsValid()) Internal_AdvanceStage(*FindActiveQuest(QuestId), Def, Next);
    }
}
```

---

## `BuildRequirementContext`

```cpp
FRequirementContext UQuestComponent::BuildRequirementContext() const
{
    // Build a FPlayerContext snapshot. Quest requirements that need
    // quest-specific data (tracker values, completion status, cooldown timestamps)
    // retrieve it via PlayerState->FindComponentByClass<UQuestComponent>()
    // inside their own Evaluate override — not via the context.
    FPlayerContext CtxData;
    CtxData.PlayerState = GetOwner<APlayerState>();
    CtxData.World       = GetWorld();
    return FRequirementContext::Make(CtxData);
}
```

**Why no payload injection.** Prior versions injected tracker counters into `FRequirementContext::PersistedData` via `FRequirementPayload`. This was removed because:
- It made the requirement system carry tracker concerns it should not know about.
- Quest requirements that need component data call `FindComponentByClass<UQuestComponent>()` once per evaluation — the cost is a pointer walk through a small component list, acceptable for non-hot-path evaluation.
- The requirement system v2 has no `PersistedData` field on `FRequirementContext`.

---

## BeginPlay and Login Validation

```cpp
void UQuestComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!QuestConfig.IsNull())
        LoadedConfig = QuestConfig.LoadSynchronous();

    if (GetOwnerRole() == ROLE_Authority)
    {
        // Server: remove disabled quests first, then register unlock watchers.
        // Watcher registration is deferred until all async definition loads complete.
        ValidateActiveQuestsOnLogin();
    }
    else if (IsOwner())
    {
        // Owning client only: register ClientValidated completion watchers.
        // These allow the client to fire ServerRPC_RequestValidation when
        // stage requirements pass, giving responsive UI feedback.
        RegisterClientValidatedCompletionWatchers();
    }
}
```

---

## `ValidateActiveQuestsOnLogin`

Removes disabled active quests before registering watchers. Uses a shared counter to detect when all async definition loads have resolved.

```cpp
void UQuestComponent::ValidateActiveQuestsOnLogin()
{
    TArray<FGameplayTag> QuestIds;
    for (const FQuestRuntime& R : ActiveQuests.Items)
        QuestIds.Add(R.QuestId);

    if (QuestIds.IsEmpty())
    {
        RegisterUnlockWatchers();
        return;
    }

    TSharedRef<int32> Pending = MakeShared<int32>(QuestIds.Num());
    TWeakObjectPtr<UQuestComponent> WeakThis = this;

    UQuestRegistrySubsystem* Registry =
        GetGameInstance()->GetSubsystem<UQuestRegistrySubsystem>();

    for (const FGameplayTag& QuestId : QuestIds)
    {
        Registry->GetOrLoadDefinitionAsync(QuestId,
            [WeakThis, QuestId, Pending](const UQuestDefinition* Def)
            {
                if (UQuestComponent* QC = WeakThis.Get())
                {
                    if (!Def || !Def->bEnabled)
                    {
                        // Non-destructive removal — no QuestCompletedTag added.
                        QC->ActiveQuests.Items.RemoveAll(
                            [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
                        UE_LOG(LogQuest, Warning,
                            TEXT("Removed disabled quest '%s' on login."),
                            *QuestId.ToString());
                    }

                    --(*Pending);
                    if (*Pending == 0)
                    {
                        if (QC->ActiveQuests.Items.Num() > 0)
                            QC->ActiveQuests.MarkArrayDirty();
                        QC->RegisterUnlockWatchers();
                    }
                }
            });
    }
}
```

---

## `RegisterUnlockWatchers`

Called after `ValidateActiveQuestsOnLogin` completes. Iterates all known quest definitions to find candidates, then:
1. Registers a reactive watcher via `URequirementList::RegisterWatch`.
2. Immediately evaluates the list imperatively to establish a baseline (login snapshot).

```cpp
void UQuestComponent::RegisterUnlockWatchers()
{
    UQuestRegistrySubsystem* Registry =
        GetGameInstance()->GetSubsystem<UQuestRegistrySubsystem>();

    // Iterate all known quest definitions from the registry.
    Registry->IterateAllDefinitions(
        [this](const FGameplayTag& QuestId, const UQuestDefinition* Def)
        {
            if (!ShouldWatchUnlock(QuestId, Def)) return;
            if (!Def->UnlockRequirements) return;

            TWeakObjectPtr<UQuestComponent> WeakThis = this;

            FEventWatchHandle Handle = Def->UnlockRequirements->RegisterWatch(
                this,
                [WeakThis, QuestId](bool bPassed)
                {
                    UQuestComponent* QC = WeakThis.Get();
                    if (!QC) return;

                    if (bPassed)
                    {
                        // Server: notify client that quest is now available.
                        // Client will trigger ServerRPC_AcceptQuest on interaction.
                        QC->ClientRPC_NotifyQuestEvent(QuestId,
                            EQuestEventType::BecameAvailable);
                    }
                    else
                    {
                        QC->ClientRPC_NotifyQuestEvent(QuestId,
                            EQuestEventType::BecameUnavailable);
                    }
                });

            UnlockWatcherHandles.Add(QuestId, Handle);

            // Baseline check: evaluate immediately at login without waiting
            // for an event to fire. Informs the client of current availability.
            FRequirementContext Ctx = BuildRequirementContext();
            FRequirementResult Result = Def->UnlockRequirements->Evaluate(Ctx);
            ClientRPC_NotifyQuestEvent(QuestId,
                Result.bPassed
                    ? EQuestEventType::BecameAvailable
                    : EQuestEventType::BecameUnavailable);
        });
}
```

> **Note on `IterateAllDefinitions`.** `UQuestRegistrySubsystem` must expose an iteration method over all known quest asset IDs. Only definitions that are already resident in memory are iterated synchronously; the registry loads others on demand elsewhere. Unlock watchers for quests whose definitions are not yet loaded will be registered lazily when those definitions first load (see `UQuestRegistrySubsystem` spec).

---

## `RegisterClientValidatedCompletionWatchers`

Called on the owning client in `BeginPlay`. Registers `ClientValidated` completion requirement watchers for all currently active quests. When requirements pass client-side, fires `ServerRPC_RequestValidation`.

```cpp
void UQuestComponent::RegisterClientValidatedCompletionWatchers()
{
    // Client only has access to its own replicated ActiveQuests.
    // Definitions for active quests must already be loaded (they loaded
    // at accept time server-side and replicated).
    UQuestRegistrySubsystem* Registry =
        GetGameInstance()->GetSubsystem<UQuestRegistrySubsystem>();

    for (const FQuestRuntime& Runtime : ActiveQuests.Items)
    {
        const UQuestDefinition* Def =
            Registry->FindLoadedDefinition(Runtime.QuestId);
        if (!Def) continue;

        const UQuestStageDefinition* Stage =
            Def->FindStage(Runtime.CurrentStageTag);
        if (!Stage || !Stage->CompletionRequirements) continue;

        // Only register if this list is ClientValidated.
        if (Def->CheckAuthority != EQuestCheckAuthority::ClientValidated) continue;

        TWeakObjectPtr<UQuestComponent> WeakThis = this;
        FGameplayTag QuestId    = Runtime.QuestId;
        FGameplayTag StageTag   = Runtime.CurrentStageTag;

        Stage->CompletionRequirements->RegisterWatch(
            this,
            [WeakThis, QuestId, StageTag](bool bPassed)
            {
                UQuestComponent* QC = WeakThis.Get();
                if (!QC || !bPassed) return;

                // Tell the server: client believes this stage is complete.
                // Server re-evaluates from authoritative context before acting.
                QC->ServerRPC_RequestValidation(QuestId, StageTag);
            });
        // Note: completion watcher handles are NOT stored on the client.
        // They are unregistered implicitly when the owning list asset is
        // unloaded, or explicitly when the component is destroyed.
        // For precision control, store handles in a parallel TMap if needed.
    }
}
```

---

## Stage Transition Logic (ResolveNextStage)

```cpp
FGameplayTag UQuestComponent::ResolveNextStage(
    const FQuestRuntime& Runtime, const UQuestDefinition* Def) const
{
    FRequirementContext Ctx = BuildRequirementContext();

    return Def->StageGraph->FindFirstPassingTransition(
        Runtime.CurrentStageTag,
        &Ctx); // UQuestTransitionRule casts ContextObject to FRequirementContext*
}
```

---

## Validation Flow (ServerRPC_RequestValidation)

```
ServerRPC_RequestValidation(QuestId, StageTag)
  1. Find FQuestRuntime -> not found: silent reject
  2. Runtime.CurrentStageTag != StageTag
     -> ClientRPC_NotifyValidationRejected(StageMismatch)
  3. UQuestRegistrySubsystem::GetOrLoadDefinitionAsync(QuestId, callback)
  IN CALLBACK:
  4. bEnabled check -> reject if false
  5. Find UQuestStageDefinition for StageTag
  6. BuildRequirementContext() — server-side, authoritative
  7. Stage->CompletionRequirements->Evaluate(Ctx):
     On Fail -> ClientRPC_NotifyValidationRejected(ConditionFailed)
     On Pass ->
       if bIsCompletionState -> Internal_CompleteQuest
       if bIsFailureState    -> Internal_FailQuest
       else ResolveNextStage -> Internal_AdvanceStage(NextStageTag)
```

---

## Complete / Fail Flows

```
Internal_CompleteQuest(Runtime, Def)
  1. Runtime.LastCompletedTimestamp = FDateTime::UtcNow().ToUnixTimestamp()
  2. UQuestRegistrySubsystem::ReleaseReference(QuestId)
  3. Switch on Lifecycle:
     SingleAttempt | RetryUntilComplete:
       Remove from ActiveQuests
       CompletedQuestTags.AddTag(Def->QuestCompletedTag)
       Unregister UnlockWatcherHandle (quest permanently closed)
     RetryAndAssist | Evergreen:
       Remove from ActiveQuests
       Re-register unlock watcher — cooldown requirement in UnlockRequirements
       blocks re-accept until cooldown expires
  4. NotifyDirty(this)
  5. Broadcast GMS: GameCoreEvent.Quest.Completed
  6. ClientRPC_NotifyQuestEvent(QuestId, Completed)

Internal_FailQuest(Runtime, Def)
  1. UQuestRegistrySubsystem::ReleaseReference(QuestId)
  2. Switch on Lifecycle:
     SingleAttempt:
       CompletedQuestTags.AddTag(Def->QuestCompletedTag)
       Remove from ActiveQuests
       Unregister UnlockWatcherHandle
     RetryUntilComplete | RetryAndAssist | Evergreen:
       Remove from ActiveQuests
       Re-register unlock watcher
  3. NotifyDirty(this)
  4. Broadcast GMS: GameCoreEvent.Quest.Failed
  5. ClientRPC_NotifyQuestEvent(QuestId, Failed)
```

---

## Unlock Authority — Two Paths

```
ServerAuthoritative quests  (Def->CheckAuthority == ServerAuthoritative):
  Server unlock watcher fires (bPassed)
    -> Server fires ClientRPC_NotifyQuestEvent(BecameAvailable)
    -> Player interacts with quest giver
    -> Client fires ServerRPC_AcceptQuest
    -> Server re-evaluates UnlockRequirements at step 6 before accepting

ClientValidated quests  (Def->CheckAuthority == ClientValidated):
  Client completion watcher fires (bPassed)
    -> Client fires ServerRPC_RequestValidation
    -> Server re-evaluates CompletionRequirements from authoritative context
    -> If pass: advance/complete. If fail: ClientRPC_NotifyValidationRejected
```

> The server **always** re-evaluates requirements before taking any gameplay action regardless of authority mode. Client results are UI hints only.

---

## `UQuestConfigDataAsset`

**File:** `Quest/Data/QuestConfigDataAsset.h`

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UQuestConfigDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(ClampMin=1, ClampMax=200))
    int32 MaxActiveQuests = 20;
};
```

---

## `USharedQuestComponent`

**File:** `Quest/Components/SharedQuestComponent.h / .cpp`

Inherits `UQuestComponent`. Adds group-aware accept flow and shared tracker routing via `IGroupProvider`. Falls back to base behavior when no `IGroupProvider` is present.

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API USharedQuestComponent : public UQuestComponent
{
    GENERATED_BODY()
public:
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

    USharedQuestCoordinator* GetCoordinator() const
    {
        IGroupProvider* Provider = GetGroupProvider();
        if (!Provider) return nullptr;
        AActor* GroupActor = Provider->GetGroupActor();
        return GroupActor
            ? GroupActor->FindComponentByClass<USharedQuestCoordinator>()
            : nullptr;
    }
};
```
