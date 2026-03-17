# UQuestRegistrySubsystem

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**File:** `Quest/Subsystems/QuestRegistrySubsystem.h / .cpp` 
**Type:** `UWorldSubsystem` 
**Authority:** Server only (runs on client with reduced responsibilities)

The quest registry manages definition asset loading with reference counting, provides the server cadence reset clock for Daily/Weekly quests, and maintains the candidate quest list for unlock watcher registration.

---

## Class Declaration

```cpp
UCLASS()
class PIRATEQUESTS_API UQuestRegistrySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ── Definition Loading ───────────────────────────────────────────────────

    // Returns a loaded definition synchronously, or nullptr if not yet loaded.
    // Does NOT trigger a load. Use GetOrLoadDefinitionAsync for load-on-demand.
    const UQuestDefinition* GetDefinition(const FGameplayTag& QuestId) const;

    // Async load. Callback fires on the game thread once the asset is loaded.
    // If already loaded, callback fires on the next tick (never synchronous
    // in the same frame to keep call sites consistent).
    void GetOrLoadDefinitionAsync(
        const FGameplayTag& QuestId,
        TFunction<void(const UQuestDefinition*)> OnLoaded);

    // Called by UQuestComponent when a player accepts a quest.
    // Increments the reference count for this definition.
    // Keeps the asset resident in memory while at least one player has it active.
    void AddReference(const FGameplayTag& QuestId);

    // Called by UQuestComponent when a quest is completed, failed, or abandoned.
    // Decrements the reference count. Unloads when count reaches zero.
    void ReleaseReference(const FGameplayTag& QuestId);

    // ── Candidate Quest List ────────────────────────────────────────────────

    // Returns all quest IDs eligible for unlock watcher registration for a player.
    // Filters out quests already in CompletedQuestTags (SingleAttempt or permanently closed).
    // Does NOT filter already-active quests — caller handles that.
    // Candidates are partitioned by UQuestDefinition::QuestCategories for scalable
    // watcher registration (e.g. only register Chapter 1 quests until chapter unlocks).
    TArray<FGameplayTag> GetCandidateQuestIds(
        const FGameplayTagContainer& CompletedTags) const;

    // ── Cadence Reset Clock (Server Only) ─────────────────────────────────

    // Returns the Unix timestamp of the most recent Daily reset (00:00 UTC today).
    int64 GetLastDailyResetTimestamp() const  { return LastDailyResetTimestamp; }

    // Returns the Unix timestamp of the most recent Weekly reset (Monday 00:00 UTC).
    int64 GetLastWeeklyResetTimestamp() const { return LastWeeklyResetTimestamp; }

    // ── Registered Quest Registry ────────────────────────────────────────────

    // All known quest IDs, populated from Asset Manager at Initialize().
    // Soft references — not loaded at startup.
    // Used to build the candidate list without loading every definition.
    const TArray<FPrimaryAssetId>& GetAllQuestAssetIds() const
    {
        return AllQuestAssetIds;
    }

private:
    struct FQuestLoadState
    {
        TObjectPtr<const UQuestDefinition> Definition = nullptr;
        int32 RefCount = 0;
        TSharedPtr<FStreamableHandle> LoadHandle;
        TArray<TFunction<void(const UQuestDefinition*)>> PendingCallbacks;
    };

    // Keyed by QuestId FGameplayTag name
    TMap<FGameplayTag, FQuestLoadState> LoadedDefinitions;

    TArray<FPrimaryAssetId> AllQuestAssetIds;

    int64 LastDailyResetTimestamp  = 0;
    int64 LastWeeklyResetTimestamp = 0;

    FTimerHandle CadenceCheckTimer;

    void TickCadenceCheck();
    void ComputeResetTimestamps();
    void BroadcastCadenceReset(EQuestResetCadence Cadence);

    // On cadence reset: iterate all active UQuestComponents, flush
    // CompletedQuestTags for quests whose cadence matches.
    void OnDailyReset();
    void OnWeeklyReset();
};
```

---

## Initialization

```cpp
void UQuestRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Enumerate all registered QuestDefinition primary assets via Asset Manager.
    // This does NOT load them — only builds the ID list for candidate queries.
    UAssetManager& AM = UAssetManager::Get();
    AM.GetPrimaryAssetIdList(
        FPrimaryAssetType(TEXT("QuestDefinition")),
        AllQuestAssetIds);

    // Compute initial reset timestamps and start cadence ticker.
    ComputeResetTimestamps();
    GetWorld()->GetTimerManager().SetTimer(
        CadenceCheckTimer,
        this,
        &UQuestRegistrySubsystem::TickCadenceCheck,
        60.0f,  // Check every minute — precise enough for Daily/Weekly granularity
        true);
}
```

---

## Reference-Counted Loading

```cpp
void UQuestRegistrySubsystem::GetOrLoadDefinitionAsync(
    const FGameplayTag& QuestId,
    TFunction<void(const UQuestDefinition*)> OnLoaded)
{
    FQuestLoadState& State = LoadedDefinitions.FindOrAdd(QuestId);

    if (State.Definition)
    {
        // Already loaded. Defer callback to next tick for call-site consistency.
        FTimerHandle Dummy;
        GetWorld()->GetTimerManager().SetTimerForNextTick(
            [OnLoaded, Def = State.Definition]() { OnLoaded(Def); });
        return;
    }

    State.PendingCallbacks.Add(MoveTemp(OnLoaded));

    if (State.LoadHandle) return; // Already in flight

    // Resolve soft path via Asset Manager tag -> asset path mapping.
    FSoftObjectPath AssetPath = ResolveQuestPath(QuestId);
    if (!AssetPath.IsValid())
    {
        UE_LOG(LogQuest, Warning,
            TEXT("No asset registered for QuestId: %s"), *QuestId.ToString());
        for (auto& CB : State.PendingCallbacks) CB(nullptr);
        State.PendingCallbacks.Empty();
        return;
    }

    State.LoadHandle = UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
        AssetPath,
        [this, QuestId]()
        {
            FQuestLoadState& S = LoadedDefinitions[QuestId];
            S.Definition = Cast<UQuestDefinition>(
                S.LoadHandle->GetLoadedAsset());
            for (auto& CB : S.PendingCallbacks) CB(S.Definition);
            S.PendingCallbacks.Empty();
        });
}

void UQuestRegistrySubsystem::ReleaseReference(const FGameplayTag& QuestId)
{
    FQuestLoadState* State = LoadedDefinitions.Find(QuestId);
    if (!State) return;

    --State->RefCount;
    if (State->RefCount <= 0)
    {
        // No players holding this quest active. Release the streamable handle.
        // GC will reclaim the asset on the next collection pass.
        State->LoadHandle.Reset();
        State->Definition = nullptr;
        State->RefCount   = 0;
        // Keep the map entry — avoids repeated key insertion on rapid accept/abandon.
    }
}
```

---

## Cadence Reset

```cpp
void UQuestRegistrySubsystem::TickCadenceCheck()
{
    int64 Now = FDateTime::UtcNow().ToUnixTimestamp();

    int64 NewDaily  = ComputeLastDailyReset(Now);
    int64 NewWeekly = ComputeLastWeeklyReset(Now);

    if (NewDaily > LastDailyResetTimestamp)
    {
        LastDailyResetTimestamp = NewDaily;
        OnDailyReset();
    }
    if (NewWeekly > LastWeeklyResetTimestamp)
    {
        LastWeeklyResetTimestamp = NewWeekly;
        OnWeeklyReset();
    }
}

void UQuestRegistrySubsystem::OnDailyReset()
{
    // Iterate all APlayerState actors in the world.
    // For each UQuestComponent, flush CompletedQuestTags entries
    // for Daily quests (those whose ResetCadence == Daily).
    // The watcher system will re-evaluate unlock requirements next flush.
    for (APlayerState* PS : TActorRange<APlayerState>(GetWorld()))
    {
        if (UQuestComponent* QC =
            PS->FindComponentByClass<UQuestComponent>())
        {
            QC->FlushCadenceResets(EQuestResetCadence::Daily);
        }
    }

    // Broadcast GMS so other systems (journal, UI) can react.
    // GameCoreEvent.Quest.DailyReset
}
```

> **Note:** `FlushCadenceResets` removes the quest's `QuestCompletedTag` from `CompletedQuestTags` and re-registers the unlock watcher. It does NOT re-add the quest to `ActiveQuests` — the player must accept again.

---

## Asset Manager Configuration

Add to `DefaultGame.ini`:

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="QuestDefinition",
    AssetBaseClass=/Script/PirateGame.QuestDefinition,
    bHasBlueprintClasses=False,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Quests"))
)
```
