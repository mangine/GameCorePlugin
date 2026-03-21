# UQuestComponent

**File:** `Quest/Components/QuestComponent.h / .cpp`
**Type:** `UActorComponent` on `APlayerState`
**Implements:** `IPersistableComponent`

The per-player solo quest runtime. Zero knowledge of groups, parties, or sharing. Exposes a public server-side API for tracker increments — has no GMS subscriptions of its own. `USharedQuestComponent` inherits this class and adds group-aware behavior as a fully optional extension.

---

## Class Declaration

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnQuestEventDelegate, FGameplayTag, QuestId, EQuestEventType, EventType);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnTrackerUpdatedDelegate,
    FGameplayTag, QuestId, FGameplayTag, TrackerKey, int32, NewValue);

UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class YOURGAME_API UQuestComponent
    : public UActorComponent
    , public IPersistableComponent
{
    GENERATED_BODY()
public:

    // ── Configuration ────────────────────────────────────────────────────────

    // Quest system configuration asset. Loaded synchronously at BeginPlay.
    // Controls MaxActiveQuests and other tunables.
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

    // Entry point for all tracker progress. Called by external bridge components.
    // Server-only. No GMS subscriptions inside this component.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    virtual void Server_IncrementTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    // Admin / debug helpers. Server-only.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    void Server_ForceCompleteQuest(const FGameplayTag& QuestId);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    void Server_ForceFailQuest(const FGameplayTag& QuestId);

    // Removes CompletedQuestTag for quests matching the cadence,
    // then re-registers their unlock watchers.
    // Called by UQuestRegistrySubsystem on Daily/Weekly reset.
    void FlushCadenceResets(EQuestResetCadence Cadence);

    FQuestRuntime*       FindActiveQuest(const FGameplayTag& QuestId);
    const FQuestRuntime* FindActiveQuest(const FGameplayTag& QuestId) const;

    int32 GetMaxActiveQuests() const;

    // ── RPCs ───────────────────────────────────────────────────────────────────

    UFUNCTION(Server, Reliable)
    virtual void ServerRPC_AcceptQuest(FGameplayTag QuestId);

    UFUNCTION(Server, Reliable)
    void ServerRPC_AbandonQuest(FGameplayTag QuestId);

    // ClientValidated completion path: client watcher passed, requesting server validation.
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
    // Returned by URequirementList::RegisterWatch.
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

    // Evaluates CompletionRequirements imperatively.
    // Called by Server_IncrementTracker when a tracker reaches EffectiveTarget.
    void EvaluateCompletionRequirementsNow(const FGameplayTag& QuestId);

    // Builds FRequirementContext with FQuestEvaluationContext in Data.
    FRequirementContext BuildRequirementContext() const;

    // Resolves the next stage tag using StageGraph::FindFirstPassingTransition.
    FGameplayTag ResolveNextStage(const FQuestRuntime& Runtime,
                                  const UQuestDefinition* Def) const;

    // Step 1 of server BeginPlay: remove disabled active quests, then call
    // RegisterUnlockWatchers when all async loads complete.
    void ValidateActiveQuestsOnLogin();

    // Step 2 of server BeginPlay: register unlock watchers for all
    // candidate quests + immediate baseline evaluation.
    void RegisterUnlockWatchers();

    // Called on owning client in BeginPlay. Registers ClientValidated
    // completion watchers for currently active quests.
    // Also called from FQuestRuntime::PostReplicatedAdd for quests accepted
    // after login (fixes KI-3).
    void RegisterClientValidatedCompletionWatcher(const FQuestRuntime& Runtime);
    void RegisterClientValidatedCompletionWatchers();

    // Returns true if this quest should have an unlock watcher registered.
    bool ShouldWatchUnlock(const FGameplayTag& QuestId,
                           const UQuestDefinition* Def) const;
};
```

---

## `ShouldWatchUnlock`

```cpp
bool UQuestComponent::ShouldWatchUnlock(
    const FGameplayTag& QuestId, const UQuestDefinition* Def) const
{
    if (!Def || !Def->bEnabled) return false;
    if (FindActiveQuest(QuestId) != nullptr) return false;

    switch (Def->Lifecycle)
    {
    case EQuestLifecycle::SingleAttempt:
    case EQuestLifecycle::RetryUntilComplete:
        if (CompletedQuestTags.HasTag(Def->QuestCompletedTag))
            return false;
        break;
    case EQuestLifecycle::RetryAndAssist:
    case EQuestLifecycle::Evergreen:
        // Always re-watchable. Cooldown expressed as URequirement_QuestCooldown
        // in UnlockRequirements — not a gate here.
        break;
    }
    return true;
}
```

---

## `BeginPlay`

```cpp
void UQuestComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!QuestConfig.IsNull())
        LoadedConfig = QuestConfig.LoadSynchronous();

    if (GetOwnerRole() == ROLE_Authority)
    {
        // Server: validate active quests, then register unlock watchers.
        ValidateActiveQuestsOnLogin();
    }
    else if (GetOwnerRole() == ROLE_AutonomousProxy)
    {
        // Owning client only: register ClientValidated completion watchers
        // for currently-replicated active quests.
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
                UQuestComponent* QC = WeakThis.Get();
                if (!QC) return;

                if (!Def || !Def->bEnabled)
                {
                    // Non-destructive removal — no QuestCompletedTag added.
                    QC->ActiveQuests.Items.RemoveAll(
                        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
                    UE_LOG(LogQuest, Warning,
                        TEXT("Removed disabled quest '%s' on login."),
                        *QuestId.ToString());
                }

                if (--(*Pending) == 0)
                {
                    if (!QC->ActiveQuests.Items.IsEmpty())
                        QC->ActiveQuests.MarkArrayDirty();
                    QC->RegisterUnlockWatchers();
                }
            });
    }
}
```

---

## `RegisterUnlockWatchers`

```cpp
void UQuestComponent::RegisterUnlockWatchers()
{
    UQuestRegistrySubsystem* Registry =
        GetGameInstance()->GetSubsystem<UQuestRegistrySubsystem>();

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
                    if (UQuestComponent* QC = WeakThis.Get())
                    {
                        QC->ClientRPC_NotifyQuestEvent(QuestId,
                            bPassed
                                ? EQuestEventType::BecameAvailable
                                : EQuestEventType::BecameUnavailable);
                    }
                });

            UnlockWatcherHandles.Add(QuestId, Handle);

            // Immediate baseline check — establishes current availability
            // without waiting for an event (covers offline-progression case).
            FRequirementContext Ctx = BuildRequirementContext();
            FRequirementResult Result = Def->UnlockRequirements->Evaluate(Ctx);
            ClientRPC_NotifyQuestEvent(QuestId,
                Result.bPassed
                    ? EQuestEventType::BecameAvailable
                    : EQuestEventType::BecameUnavailable);
        });
}
```

---

## Accept Flow (`ServerRPC_AcceptQuest`)

```
ServerRPC_AcceptQuest(QuestId)
  1. Capacity check: ActiveQuests.Items.Num() >= GetMaxActiveQuests() → reject
  2. Duplicate check: already in ActiveQuests → reject
  3. CompletedQuestTags pre-filter (SingleAttempt) → reject
  4. GetOrLoadDefinitionAsync(QuestId)
  IN CALLBACK:
  5. bEnabled check
  6. UnlockRequirements->Evaluate(BuildRequirementContext()) — server authoritative
     Fail → ClientRPC_NotifyValidationRejected(ConditionFailed)
  7. Create FQuestRuntime: QuestId, CurrentStageTag = StageGraph entry state
  8. Internal_InitTrackers for entry stage
  9. ActiveQuests.Items.Add, MarkArrayDirty
 10. Registry->AddReference(QuestId)
 11. NotifyDirty(this)
 12. Unregister UnlockWatcherHandle for QuestId
 13. Broadcast GMS: GameCoreEvent.Quest.Started
 14. Broadcast GMS: GameCoreEvent.Quest.StageStarted
 15. ClientRPC_NotifyQuestEvent(QuestId, Started)
```

---

## Tracker Increment Flow (`Server_IncrementTracker`)

```cpp
void UQuestComponent::Server_IncrementTracker(
    const FGameplayTag& QuestId,
    const FGameplayTag& TrackerKey,
    int32 Delta)
{
    FQuestRuntime* Runtime = FindActiveQuest(QuestId);
    if (!Runtime) return;

    FQuestTrackerEntry* Tracker = Runtime->FindTracker(TrackerKey);
    if (!Tracker) return;

    const int32 OldValue = Tracker->CurrentValue;
    Tracker->CurrentValue = FMath::Min(Tracker->CurrentValue + Delta,
                                        Tracker->EffectiveTarget);

    if (Tracker->CurrentValue == OldValue) return; // clamped, no-op

    ActiveQuests.MarkItemDirty(*Runtime);
    NotifyDirty(this); // persistence

    // Broadcast for external systems (achievements, analytics, UI).
    FQuestTrackerUpdatedPayload Payload;
    Payload.QuestId         = QuestId;
    Payload.TrackerKey      = TrackerKey;
    Payload.OldValue        = OldValue;
    Payload.NewValue        = Tracker->CurrentValue;
    Payload.EffectiveTarget = Tracker->EffectiveTarget;
    Payload.PlayerState     = GetOwner<APlayerState>();
    UGameCoreEventBus::Get(this).Broadcast(
        TAG_GameCoreEvent_Quest_TrackerUpdated, Payload);

    if (Tracker->CurrentValue >= Tracker->EffectiveTarget)
        EvaluateCompletionRequirementsNow(QuestId);
}
```

---

## `EvaluateCompletionRequirementsNow`

```cpp
void UQuestComponent::EvaluateCompletionRequirementsNow(const FGameplayTag& QuestId)
{
    const FQuestRuntime* Runtime = FindActiveQuest(QuestId);
    if (!Runtime) return;

    UQuestRegistrySubsystem* Registry =
        GetGameInstance()->GetSubsystem<UQuestRegistrySubsystem>();
    const UQuestDefinition* Def = Registry->GetDefinition(QuestId);
    if (!Def) return;

    const UQuestStageDefinition* Stage = Def->FindStage(Runtime->CurrentStageTag);
    if (!Stage || !Stage->CompletionRequirements) return;

    FRequirementContext Ctx = BuildRequirementContext();
    FRequirementResult Result = Stage->CompletionRequirements->Evaluate(Ctx);
    if (!Result.bPassed) return;

    // Mutable lookup after pass — safe because we hold authority.
    FQuestRuntime* MutableRuntime = FindActiveQuest(QuestId);
    if (!MutableRuntime) return;

    if (Stage->bIsCompletionState)   Internal_CompleteQuest(*MutableRuntime, Def);
    else if (Stage->bIsFailureState) Internal_FailQuest(*MutableRuntime, Def);
    else
    {
        const FGameplayTag Next = ResolveNextStage(*MutableRuntime, Def);
        if (Next.IsValid()) Internal_AdvanceStage(*MutableRuntime, Def, Next);
    }
}
```

---

## Complete / Fail Flows

```cpp
void UQuestComponent::Internal_CompleteQuest(
    FQuestRuntime& Runtime, const UQuestDefinition* Def)
{
    Runtime.LastCompletedTimestamp =
        FDateTime::UtcNow().ToUnixTimestamp();

    UQuestRegistrySubsystem* Registry =
        GetGameInstance()->GetSubsystem<UQuestRegistrySubsystem>();
    Registry->ReleaseReference(Runtime.QuestId);

    const bool bPermanent =
        Def->Lifecycle == EQuestLifecycle::SingleAttempt ||
        Def->Lifecycle == EQuestLifecycle::RetryUntilComplete;

    if (bPermanent)
    {
        CompletedQuestTags.AddTag(Def->QuestCompletedTag);
        if (FEventWatchHandle* H = UnlockWatcherHandles.Find(Def->QuestId))
        {
            H->Unregister();
            UnlockWatcherHandles.Remove(Def->QuestId);
        }
    }

    const FGameplayTag QuestId = Runtime.QuestId;
    ActiveQuests.Items.RemoveAll(
        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
    ActiveQuests.MarkArrayDirty();

    if (!bPermanent)
    {
        // Re-register unlock watcher — cooldown in UnlockRequirements gates re-accept.
        if (const UQuestDefinition* Loaded = Registry->GetDefinition(QuestId))
            if (ShouldWatchUnlock(QuestId, Loaded))
                RegisterUnlockWatcherForQuest(QuestId, Loaded);
    }

    NotifyDirty(this);

    FQuestCompletedPayload Payload;
    Payload.QuestId      = QuestId;
    Payload.PlayerState  = GetOwner<APlayerState>();
    Payload.RewardTable  = bPermanent
        ? Def->FirstTimeRewardTable : Def->RepeatingRewardTable;
    UGameCoreEventBus::Get(this).Broadcast(
        TAG_GameCoreEvent_Quest_Completed, Payload);

    ClientRPC_NotifyQuestEvent(QuestId, EQuestEventType::Completed);
}

void UQuestComponent::Internal_FailQuest(
    FQuestRuntime& Runtime, const UQuestDefinition* Def)
{
    UQuestRegistrySubsystem* Registry =
        GetGameInstance()->GetSubsystem<UQuestRegistrySubsystem>();
    Registry->ReleaseReference(Runtime.QuestId);

    const bool bPermanent =
        Def->Lifecycle == EQuestLifecycle::SingleAttempt;

    if (bPermanent)
    {
        CompletedQuestTags.AddTag(Def->QuestCompletedTag);
        if (FEventWatchHandle* H = UnlockWatcherHandles.Find(Def->QuestId))
        {
            H->Unregister();
            UnlockWatcherHandles.Remove(Def->QuestId);
        }
    }

    const FGameplayTag QuestId = Runtime.QuestId;
    ActiveQuests.Items.RemoveAll(
        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
    ActiveQuests.MarkArrayDirty();

    if (!bPermanent)
    {
        if (const UQuestDefinition* Loaded = Registry->GetDefinition(QuestId))
            if (ShouldWatchUnlock(QuestId, Loaded))
                RegisterUnlockWatcherForQuest(QuestId, Loaded);
    }

    NotifyDirty(this);

    FQuestFailedPayload Payload;
    Payload.QuestId           = QuestId;
    Payload.PlayerState       = GetOwner<APlayerState>();
    Payload.bPermanentlyClosed = bPermanent;
    UGameCoreEventBus::Get(this).Broadcast(
        TAG_GameCoreEvent_Quest_Failed, Payload);

    ClientRPC_NotifyQuestEvent(QuestId, EQuestEventType::Failed);
}
```

---

## `USharedQuestComponent`

**File:** `Quest/Components/SharedQuestComponent.h / .cpp`

Inherits `UQuestComponent`. Overrides `ServerRPC_AcceptQuest` and `Server_IncrementTracker` to route through `USharedQuestCoordinator` when a group provider is available. Falls back to base behavior when no `IGroupProvider` is present on the owner.

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class YOURGAME_API USharedQuestComponent : public UQuestComponent
{
    GENERATED_BODY()
public:
    // Called by USharedQuestCoordinator to apply a de-scaled snapshot
    // when this member leaves a shared quest.
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

> **Solo fallback:** If `GetCoordinator()` returns `nullptr` (no group provider or no coordinator on the group actor), both overrides delegate to the `UQuestComponent` base implementation. `USharedQuestComponent` is a drop-in replacement — it adds no overhead when there is no active group.
