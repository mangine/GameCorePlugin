# UQuestRegistrySubsystem

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**File:** `Quest/Subsystems/QuestRegistrySubsystem.h / .cpp` 
**Type:** `UGameInstanceSubsystem` 
**Authority:** Runs on both server and client. Server owns the cadence clock and definition loading. Client uses the subsystem for cadence timestamps (for UI cooldown display) and definition lookup when already loaded.

> **Why `UGameInstanceSubsystem` not `UWorldSubsystem`?** Quest definitions and the cadence clock are not world-specific. Using `UWorldSubsystem` would destroy and recreate the subsystem on every world transition (loading screens, sublevel changes), losing ref-counted definitions and forcing redundant asset reloads. `UGameInstanceSubsystem` persists across world transitions.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UQuestRegistrySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ── Definition Loading ────────────────────────────────────────────────────────

    // Returns a loaded definition synchronously, or nullptr if not yet loaded.
    // Does NOT trigger a load.
    const UQuestDefinition* GetDefinition(const FGameplayTag& QuestId) const;

    // Async load. Callback fires on the game thread.
    // If already loaded, callback fires synchronously in the same frame
    // (not deferred) so callers can rely on immediate completion when cached.
    // Callers must handle both the synchronous (already-loaded) and asynchronous
    // (first-load) paths — the callback may fire before or after the call returns.
    void GetOrLoadDefinitionAsync(
        const FGameplayTag& QuestId,
        TFunction<void(const UQuestDefinition*)> OnLoaded);

    // Called by UQuestComponent on quest accept. Increments ref count.
    void AddReference(const FGameplayTag& QuestId);

    // Called by UQuestComponent on complete/fail/abandon. Decrements ref count.
    // Unloads definition when count reaches zero.
    void ReleaseReference(const FGameplayTag& QuestId);

    // Returns candidate quest IDs for unlock watcher registration.
    // Filters out IDs in CompletedTags. Does not filter already-active quests.
    // Server only — client does not register unlock watchers independently.
    TArray<FGameplayTag> GetCandidateQuestIds(
        const FGameplayTagContainer& CompletedTags) const;

    // ── Cadence Reset Clock ────────────────────────────────────────────────────
    // Available on both server and client (client uses for UI countdown math).

    int64 GetLastDailyResetTimestamp()  const { return LastDailyResetTimestamp; }
    int64 GetLastWeeklyResetTimestamp() const { return LastWeeklyResetTimestamp; }

    // ── All Quest Asset IDs ────────────────────────────────────────────────────────

    const TArray<FPrimaryAssetId>& GetAllQuestAssetIds() const
    { return AllQuestAssetIds; }

private:
    struct FQuestLoadState
    {
        TObjectPtr<const UQuestDefinition> Definition = nullptr;
        int32 RefCount = 0;
        TSharedPtr<FStreamableHandle> LoadHandle;
        TArray<TFunction<void(const UQuestDefinition*)>> PendingCallbacks;
    };

    TMap<FGameplayTag, FQuestLoadState> LoadedDefinitions;
    TArray<FPrimaryAssetId>             AllQuestAssetIds;

    int64        LastDailyResetTimestamp  = 0;
    int64        LastWeeklyResetTimestamp = 0;
    FTimerHandle CadenceCheckTimer;

    // Resolves the FSoftObjectPath for a QuestId tag by matching against
    // AllQuestAssetIds loaded from the Asset Manager at Initialize.
    // Convention: asset name matches QuestId leaf tag name (e.g. Quest.Id.TreasureHunt
    // maps to asset named TreasureHunt in /Game/Quests/).
    FSoftObjectPath ResolveQuestPath(const FGameplayTag& QuestId) const;

    void TickCadenceCheck();
    void ComputeResetTimestamps();
    void OnDailyReset();
    void OnWeeklyReset();
    void FlushCadenceResetsForAllPlayers(EQuestResetCadence Cadence);
};
```

---

## Initialization

```cpp
void UQuestRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UAssetManager& AM = UAssetManager::Get();
    AM.GetPrimaryAssetIdList(
        FPrimaryAssetType(TEXT("QuestDefinition")),
        AllQuestAssetIds);

    ComputeResetTimestamps();

    // Cadence clock only needed server-side.
    // Client reads timestamps from subsystem for UI math only.
    if (GetGameInstance()->IsDedicatedServerInstance()
        || GetGameInstance()->GetWorld()->GetNetMode() < NM_Client)
    {
        GetGameInstance()->GetTimerManager().SetTimer(
            CadenceCheckTimer, this,
            &UQuestRegistrySubsystem::TickCadenceCheck,
            60.0f, true);
    }
}
```

---

## `ResolveQuestPath` — Tag to Asset Path

```cpp
FSoftObjectPath UQuestRegistrySubsystem::ResolveQuestPath(
    const FGameplayTag& QuestId) const
{
    // The asset name is the leaf node of the QuestId tag.
    // e.g. Quest.Id.TreasureHunt -> asset name "TreasureHunt"
    const FName LeafName = FName(*QuestId.GetTagName().ToString()
        .RightChop(QuestId.GetTagName().ToString().Find(
            TEXT("."), ESearchCase::IgnoreCase,
            ESearchDir::FromEnd) + 1));

    UAssetManager& AM = UAssetManager::Get();
    for (const FPrimaryAssetId& AssetId : AllQuestAssetIds)
    {
        if (AssetId.PrimaryAssetName == LeafName)
        {
            FSoftObjectPath Path;
            AM.GetPrimaryAssetPath(AssetId, Path);
            return Path;
        }
    }
    return FSoftObjectPath();
}
```

> **Convention enforced by `IsDataValid`:** `UQuestDefinition::QuestId` leaf tag name must match the asset file name. e.g. a quest with `QuestId = Quest.Id.TreasureHunt` must be in an asset named `TreasureHunt`. This is validated at cook time via `IsDataValid`.

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
        // Already loaded — fire callback synchronously in this frame.
        // Callers must handle both sync and async completion.
        OnLoaded(State.Definition);
        return;
    }

    State.PendingCallbacks.Add(MoveTemp(OnLoaded));
    if (State.LoadHandle) return; // Load already in flight

    FSoftObjectPath AssetPath = ResolveQuestPath(QuestId);
    if (!AssetPath.IsValid())
    {
        UE_LOG(LogQuest, Warning,
            TEXT("GetOrLoadDefinitionAsync: No asset for QuestId '%s'."),
            *QuestId.ToString());
        for (auto& CB : State.PendingCallbacks) CB(nullptr);
        State.PendingCallbacks.Empty();
        return;
    }

    State.LoadHandle = UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
        AssetPath,
        [this, QuestId]()
        {
            FQuestLoadState& S = LoadedDefinitions[QuestId];
            S.Definition = Cast<UQuestDefinition>(S.LoadHandle->GetLoadedAsset());
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
        State->LoadHandle.Reset();
        State->Definition = nullptr;
        State->RefCount   = 0;
    }
}
```

---

## Cadence Reset

```cpp
void UQuestRegistrySubsystem::TickCadenceCheck()
{
    const int64 Now     = FDateTime::UtcNow().ToUnixTimestamp();
    const int64 NewDaily  = ComputeLastDailyReset(Now);
    const int64 NewWeekly = ComputeLastWeeklyReset(Now);

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
    FlushCadenceResetsForAllPlayers(EQuestResetCadence::Daily);
    // Broadcast GMS: GameCoreEvent.Quest.DailyReset
}

void UQuestRegistrySubsystem::FlushCadenceResetsForAllPlayers(
    EQuestResetCadence Cadence)
{
    // Iterate all worlds owned by this game instance to cover all maps.
    for (const FWorldContext& Ctx :
         GEngine->GetWorldContexts())
    {
        UWorld* World = Ctx.World();
        if (!World || World->GetNetMode() > NM_ListenServer) continue;

        for (APlayerState* PS : TActorRange<APlayerState>(World))
        {
            if (UQuestComponent* QC =
                PS->FindComponentByClass<UQuestComponent>())
            {
                QC->FlushCadenceResets(Cadence);
            }
        }
    }
}
```

---

## Client Responsibilities

On the client, `UQuestRegistrySubsystem`:
- Holds `LastDailyResetTimestamp` and `LastWeeklyResetTimestamp` — populated at login via replication or initial RPC from the server. The UI uses these for cooldown countdown math.
- Does **not** run the cadence tick timer.
- Does **not** call `FlushCadenceResetsForAllPlayers`.
- Does **not** manage `LoadedDefinitions` or reference counts — definitions are server-loaded only.
- May call `GetDefinition` for already-loaded definitions if the server has pushed one (e.g. for quest UI display), but this is opportunistic, not required.

---

## Asset Manager Configuration

Add to `DefaultGame.ini` (replace `YourGame` with the actual module name):

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="QuestDefinition",
    AssetBaseClass=/Script/GameCore.QuestDefinition,
    bHasBlueprintClasses=False,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Quests"))
)
```
