# UQuestRegistrySubsystem

**File:** `Quest/Subsystems/QuestRegistrySubsystem.h / .cpp`
**Type:** `UGameInstanceSubsystem`
**Authority:** Server owns definition loading, ref-counting, and cadence clock. Client reads timestamps for UI cooldown display and does definition lookup opportunistically.

> **Why `UGameInstanceSubsystem`?** Quest definitions and the cadence clock are not world-specific. A `UWorldSubsystem` would destroy and recreate on every world transition (loading screens, sublevel changes), losing the definition cache and forcing redundant asset reloads.

---

## Class Declaration

```cpp
UCLASS()
class YOURGAME_API UQuestRegistrySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ── Definition Lookup ────────────────────────────────────────────────

    // Returns loaded definition synchronously, or nullptr if not yet loaded.
    // Does NOT trigger a load.
    const UQuestDefinition* GetDefinition(const FGameplayTag& QuestId) const;

    // Async load. If already loaded, fires callback synchronously in the same
    // frame. Callers must handle both sync and async completion paths.
    void GetOrLoadDefinitionAsync(
        const FGameplayTag& QuestId,
        TFunction<void(const UQuestDefinition*)> OnLoaded);

    // ── Reference Counting ────────────────────────────────────────────────

    // Called by UQuestComponent on quest accept. Prevents unload while active.
    void AddReference(const FGameplayTag& QuestId);

    // Called by UQuestComponent on complete/fail/abandon.
    // Unloads definition when RefCount reaches zero.
    void ReleaseReference(const FGameplayTag& QuestId);

    // ── Iteration ────────────────────────────────────────────────────────

    // Iterates all resident quest definitions (those currently in memory).
    // Called by UQuestComponent::RegisterUnlockWatchers.
    // Note: definitions not yet loaded are skipped — UQuestComponent will
    // pick them up lazily when they load (see KI-1 in Architecture.md).
    void IterateAllDefinitions(
        TFunctionRef<void(const FGameplayTag&, const UQuestDefinition*)> Visitor) const;

    // Returns all known quest asset IDs as discovered at Initialize.
    const TArray<FPrimaryAssetId>& GetAllQuestAssetIds() const
    { return AllQuestAssetIds; }

    // ── Cadence Clock ────────────────────────────────────────────────────
    // Available on both server and client. Client uses for UI countdown math.

    int64 GetLastDailyResetTimestamp()  const { return LastDailyResetTimestamp; }
    int64 GetLastWeeklyResetTimestamp() const { return LastWeeklyResetTimestamp; }

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
    // Pre-built leaf-name map for O(1) tag-to-asset path resolution.
    TMap<FName, FPrimaryAssetId>        QuestAssetIdByLeafName;

    int64        LastDailyResetTimestamp  = 0;
    int64        LastWeeklyResetTimestamp = 0;
    FTimerHandle CadenceCheckTimer;

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
    AM.GetPrimaryAssetIdList(FPrimaryAssetType(TEXT("QuestDefinition")),
                              AllQuestAssetIds);

    // Pre-build the leaf-name lookup map for O(1) path resolution.
    // Resolves KI-5 from Architecture.md.
    for (const FPrimaryAssetId& AssetId : AllQuestAssetIds)
        QuestAssetIdByLeafName.Add(AssetId.PrimaryAssetName, AssetId);

    ComputeResetTimestamps();

    // Cadence clock runs server-side only.
    const UWorld* World = GetGameInstance()->GetWorld();
    if (World && World->GetNetMode() < NM_Client)
    {
        GetGameInstance()->GetTimerManager().SetTimer(
            CadenceCheckTimer, this,
            &UQuestRegistrySubsystem::TickCadenceCheck,
            60.0f, true);
    }
}
```

---

## `ResolveQuestPath` — O(1) Tag-to-Asset

```cpp
FSoftObjectPath UQuestRegistrySubsystem::ResolveQuestPath(
    const FGameplayTag& QuestId) const
{
    // Leaf tag name: Quest.Id.TreasureHunt → "TreasureHunt"
    const FString TagStr = QuestId.GetTagName().ToString();
    const int32 LastDot  = TagStr.Find(TEXT("."), ESearchCase::IgnoreCase,
                                        ESearchDir::FromEnd);
    const FName LeafName = FName(*TagStr.RightChop(LastDot + 1));

    const FPrimaryAssetId* AssetId = QuestAssetIdByLeafName.Find(LeafName);
    if (!AssetId)
    {
        UE_LOG(LogQuest, Warning,
            TEXT("ResolveQuestPath: No asset for QuestId '%s'."),
            *QuestId.ToString());
        return FSoftObjectPath();
    }

    FSoftObjectPath Path;
    UAssetManager::Get().GetPrimaryAssetPath(*AssetId, Path);
    return Path;
}
```

> **Convention:** `UQuestDefinition::QuestId` leaf tag name must match the asset file name. e.g. `Quest.Id.TreasureHunt` → asset named `TreasureHunt`. Validated by `UQuestDefinition::IsDataValid` at cook time.

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
        // Already loaded — fire synchronously in this frame.
        OnLoaded(State.Definition);
        return;
    }

    State.PendingCallbacks.Add(MoveTemp(OnLoaded));
    if (State.LoadHandle) return; // Load already in-flight

    const FSoftObjectPath AssetPath = ResolveQuestPath(QuestId);
    if (!AssetPath.IsValid())
    {
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

void UQuestRegistrySubsystem::AddReference(const FGameplayTag& QuestId)
{
    if (FQuestLoadState* State = LoadedDefinitions.Find(QuestId))
        ++State->RefCount;
}

void UQuestRegistrySubsystem::ReleaseReference(const FGameplayTag& QuestId)
{
    FQuestLoadState* State = LoadedDefinitions.Find(QuestId);
    if (!State) return;
    if (--State->RefCount <= 0)
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
    const int64 Now       = FDateTime::UtcNow().ToUnixTimestamp();
    const int64 NewDaily  = ComputeLastDailyReset(Now);   // impl: floor to midnight UTC
    const int64 NewWeekly = ComputeLastWeeklyReset(Now);  // impl: floor to Monday midnight UTC

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

    FQuestResetPayload Payload;
    Payload.Cadence = EQuestResetCadence::Daily;
    UGameCoreEventBus::Get(GetGameInstance()).Broadcast(
        TAG_GameCoreEvent_Quest_DailyReset, Payload);
}

void UQuestRegistrySubsystem::FlushCadenceResetsForAllPlayers(
    EQuestResetCadence Cadence)
{
    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
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
- Holds `LastDailyResetTimestamp` and `LastWeeklyResetTimestamp` — populated at login via replication or initial RPC from the server. Used by the UI for cooldown countdown math.
- Does **not** run the cadence tick timer.
- Does **not** call `FlushCadenceResetsForAllPlayers`.
- May call `GetDefinition` for already-loaded definitions (opportunistic, for UI display), but this is not required.

---

## DefaultGame.ini Configuration

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="QuestDefinition",
    AssetBaseClass=/Script/YourGame.QuestDefinition,
    bHasBlueprintClasses=False,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Quests"))
)
```
