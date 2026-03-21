# UJournalRegistrySubsystem

**Files:** `GameCore/Source/GameCore/Journal/JournalRegistrySubsystem.h` / `.cpp`  
**Type:** `UGameInstanceSubsystem`  
**Lives on:** Server and owning client  

Asset registry for entry tags → assets and collection definitions. Loaded once at game instance startup. Never holds per-player state.

---

## Responsibilities

- Load all `UJournalEntryDataAsset` and `UJournalCollectionDefinition` assets at startup via Asset Manager
- Maintain O(1) tag → asset resolution map
- Provide `GetCollectionProgress()` for UI progress bars (derived, never stored)
- Provide `GetCollectionMemberTags()` for `UJournalComponent::GetPage()` collection filtering
- Provide `GetEntryAsset()` for on-demand content loading by UI
- Provide `GetCollectionsForTrack()` for UI collection tab listing

---

## Class Definition

```cpp
// JournalRegistrySubsystem.h
UCLASS()
class GAMECORE_API UJournalRegistrySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Returns the soft asset reference for a given EntryTag.
    // Returns a null TSoftObjectPtr if the tag is not registered.
    TSoftObjectPtr<UJournalEntryDataAsset> GetEntryAsset(
        FGameplayTag EntryTag) const;

    // Returns collection progress: found entries vs total members (recursive).
    // AcquiredSet is UJournalComponent::GetClientAcquiredSet() on the client.
    FJournalCollectionProgress GetCollectionProgress(
        FGameplayTag CollectionTag,
        const TSet<FGameplayTag>& AcquiredSet) const;

    // Returns the flat set of all EntryTags in a collection (recursive).
    // Used by UJournalComponent::GetPage() for collection filtering.
    TSet<FGameplayTag> GetCollectionMemberTags(
        FGameplayTag CollectionTag) const;

    // Returns all loaded collection definitions for a given track.
    // Used by UI to list collections per tab.
    TArray<const UJournalCollectionDefinition*> GetCollectionsForTrack(
        FGameplayTag TrackTag) const;

private:
    void LoadAllEntryAssets();
    void LoadAllCollections();

    // Recursive helpers — use Visited guard to prevent infinite loops on malformed data.
    void CollectMemberTags(
        const UJournalCollectionDefinition* Collection,
        TSet<FGameplayTag>& OutTags,
        TSet<FGameplayTag>& Visited) const;

    FJournalCollectionProgress ComputeProgress(
        const UJournalCollectionDefinition* Collection,
        const TSet<FGameplayTag>& AcquiredSet,
        TSet<FGameplayTag>& Visited) const;

    // Tag → soft asset ref. Populated at Initialize().
    TMap<FGameplayTag, TSoftObjectPtr<UJournalEntryDataAsset>> EntryRegistry;

    // CollectionTag → loaded definition. Populated at Initialize().
    // Collections are small and fully loaded — no async needed.
    TMap<FGameplayTag, TObjectPtr<UJournalCollectionDefinition>> CollectionRegistry;

    // Streamable handles — kept alive to prevent GC of loaded assets.
    TSharedPtr<FStreamableHandle> EntryLoadHandle;
    TSharedPtr<FStreamableHandle> CollectionLoadHandle;
};
```

---

## Method Implementations

### `Initialize`

```cpp
void UJournalRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadAllEntryAssets();
    LoadAllCollections();
}

void UJournalRegistrySubsystem::Deinitialize()
{
    EntryLoadHandle.Reset();
    CollectionLoadHandle.Reset();
    EntryRegistry.Empty();
    CollectionRegistry.Empty();
    Super::Deinitialize();
}
```

### `LoadAllEntryAssets`

```cpp
void UJournalRegistrySubsystem::LoadAllEntryAssets()
{
    UAssetManager& AM = UAssetManager::Get();
    TArray<FPrimaryAssetId> AssetIds;
    AM.GetPrimaryAssetIdList(FPrimaryAssetType(TEXT("JournalEntry")), AssetIds);

    TArray<FSoftObjectPath> Paths;
    Paths.Reserve(AssetIds.Num());
    for (const FPrimaryAssetId& Id : AssetIds)
    {
        FSoftObjectPath Path;
        if (AM.GetPrimaryAssetPath(Id, Path))
            Paths.Add(Path);
    }

    // Sync load: entry assets are lightweight (FText + soft refs — no textures).
    // Bounded by content, not player count — safe at startup.
    EntryLoadHandle = UAssetManager::Get().GetStreamableManager()
        .RequestSyncLoad(Paths);

    for (const FSoftObjectPath& Path : Paths)
    {
        if (UJournalEntryDataAsset* Asset =
            Cast<UJournalEntryDataAsset>(Path.ResolveObject()))
        {
            EntryRegistry.Add(Asset->EntryTag,
                TSoftObjectPtr<UJournalEntryDataAsset>(Path));
        }
    }

    UE_LOG(LogJournal, Log, TEXT("UJournalRegistrySubsystem: loaded %d entry assets."),
        EntryRegistry.Num());
}
```

### `LoadAllCollections`

```cpp
void UJournalRegistrySubsystem::LoadAllCollections()
{
    UAssetManager& AM = UAssetManager::Get();
    TArray<FPrimaryAssetId> AssetIds;
    AM.GetPrimaryAssetIdList(FPrimaryAssetType(TEXT("JournalCollection")), AssetIds);

    TArray<FSoftObjectPath> Paths;
    Paths.Reserve(AssetIds.Num());
    for (const FPrimaryAssetId& Id : AssetIds)
    {
        FSoftObjectPath Path;
        if (AM.GetPrimaryAssetPath(Id, Path))
            Paths.Add(Path);
    }

    CollectionLoadHandle = UAssetManager::Get().GetStreamableManager()
        .RequestSyncLoad(Paths);

    for (const FSoftObjectPath& Path : Paths)
    {
        if (UJournalCollectionDefinition* Def =
            Cast<UJournalCollectionDefinition>(Path.ResolveObject()))
        {
            CollectionRegistry.Add(Def->CollectionTag, Def);
        }
    }

    UE_LOG(LogJournal, Log, TEXT("UJournalRegistrySubsystem: loaded %d collection definitions."),
        CollectionRegistry.Num());
}
```

### `GetEntryAsset`

```cpp
TSoftObjectPtr<UJournalEntryDataAsset> UJournalRegistrySubsystem::GetEntryAsset(
    FGameplayTag EntryTag) const
{
    if (const TSoftObjectPtr<UJournalEntryDataAsset>* Found = EntryRegistry.Find(EntryTag))
        return *Found;
    return TSoftObjectPtr<UJournalEntryDataAsset>();
}
```

### `GetCollectionProgress`

```cpp
FJournalCollectionProgress UJournalRegistrySubsystem::GetCollectionProgress(
    FGameplayTag CollectionTag,
    const TSet<FGameplayTag>& AcquiredSet) const
{
    const TObjectPtr<UJournalCollectionDefinition>* DefPtr =
        CollectionRegistry.Find(CollectionTag);
    if (!DefPtr || !*DefPtr) return {};

    TSet<FGameplayTag> Visited;
    return ComputeProgress(*DefPtr, AcquiredSet, Visited);
}

FJournalCollectionProgress UJournalRegistrySubsystem::ComputeProgress(
    const UJournalCollectionDefinition* Collection,
    const TSet<FGameplayTag>& AcquiredSet,
    TSet<FGameplayTag>& Visited) const
{
    // Guard against circular refs (malformed data — should be caught by IsDataValid).
    if (Visited.Contains(Collection->CollectionTag)) return {};
    Visited.Add(Collection->CollectionTag);

    FJournalCollectionProgress Result;
    for (const TSoftObjectPtr<UJournalEntryDataAsset>& MemberRef : Collection->Members)
    {
        if (const UJournalEntryDataAsset* Asset = MemberRef.Get())
        {
            ++Result.Total;
            if (AcquiredSet.Contains(Asset->EntryTag))
                ++Result.Found;
        }
    }
    for (const TSoftObjectPtr<UJournalCollectionDefinition>& SubRef : Collection->SubCollections)
    {
        if (const UJournalCollectionDefinition* Sub = SubRef.Get())
        {
            FJournalCollectionProgress SubProgress = ComputeProgress(Sub, AcquiredSet, Visited);
            Result.Found += SubProgress.Found;
            Result.Total += SubProgress.Total;
        }
    }
    return Result;
}
```

### `GetCollectionMemberTags`

```cpp
TSet<FGameplayTag> UJournalRegistrySubsystem::GetCollectionMemberTags(
    FGameplayTag CollectionTag) const
{
    TSet<FGameplayTag> Out;
    const TObjectPtr<UJournalCollectionDefinition>* DefPtr =
        CollectionRegistry.Find(CollectionTag);
    if (DefPtr && *DefPtr)
    {
        TSet<FGameplayTag> Visited;
        CollectMemberTags(*DefPtr, Out, Visited);
    }
    return Out;
}

void UJournalRegistrySubsystem::CollectMemberTags(
    const UJournalCollectionDefinition* Collection,
    TSet<FGameplayTag>& OutTags,
    TSet<FGameplayTag>& Visited) const
{
    if (Visited.Contains(Collection->CollectionTag)) return;
    Visited.Add(Collection->CollectionTag);

    for (const TSoftObjectPtr<UJournalEntryDataAsset>& MemberRef : Collection->Members)
        if (const UJournalEntryDataAsset* Asset = MemberRef.Get())
            OutTags.Add(Asset->EntryTag);

    for (const TSoftObjectPtr<UJournalCollectionDefinition>& SubRef : Collection->SubCollections)
        if (const UJournalCollectionDefinition* Sub = SubRef.Get())
            CollectMemberTags(Sub, OutTags, Visited);
}
```

### `GetCollectionsForTrack`

```cpp
TArray<const UJournalCollectionDefinition*> UJournalRegistrySubsystem::GetCollectionsForTrack(
    FGameplayTag TrackTag) const
{
    TArray<const UJournalCollectionDefinition*> Out;
    for (const auto& Pair : CollectionRegistry)
        if (Pair.Value && Pair.Value->TrackTag == TrackTag)
            Out.Add(Pair.Value);
    return Out;
}
```

---

## Asset Manager Configuration

```ini
; DefaultGame.ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="JournalEntry",
    AssetBaseClass=/Script/GameCore.JournalEntryDataAsset,
    bHasBlueprintClasses=True,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Journal/Entries"))
)
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="JournalCollection",
    AssetBaseClass=/Script/GameCore.JournalCollectionDefinition,
    bHasBlueprintClasses=False,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Journal/Collections"))
)
```

---

## Notes

- `FStreamableHandle` references are kept as `TSharedPtr` to prevent GC of loaded entry assets. Without this, UE may collect the assets after the sync load handle goes out of scope.
- Entry assets are sync-loaded at startup because they are content-count-bounded (not player-count-bounded) and are lightweight (no textures — textures remain soft refs inside the asset).
- The registry has no per-player state. It is safe to share across all player connections.
