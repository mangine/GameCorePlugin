# UJournalRegistrySubsystem

**Sub-page of:** [Journal System](../Journal%20System.md)

**Type:** `UGameInstanceSubsystem`  
**Lives on:** Server and owning client  
**Purpose:** Asset registry for entry tags → assets, and collection loading + progress queries.

---

## Responsibilities

- Load all `UJournalEntryDataAsset` and `UJournalCollectionDefinition` assets at startup via Asset Manager
- Maintain `TMap<FGameplayTag, TSoftObjectPtr<UJournalEntryDataAsset>>` for O(1) tag → asset resolution
- Maintain `TMap<FGameplayTag, UJournalCollectionDefinition*>` for collection access
- Provide `GetCollectionProgress()` for UI progress bars
- Provide `GetCollectionMemberTags()` for pagination filtering
- Provide `GetEntryAsset()` for on-demand content loading by UI

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
    // Returns nullptr TSoftObjectPtr if not registered.
    TSoftObjectPtr<UJournalEntryDataAsset> GetEntryAsset(
        FGameplayTag EntryTag) const;

    // Returns collection progress: found entries vs total members (recursive).
    // AcquiredSet is the client's ClientAcquiredSet — built from local Entries.
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

    // Recursive helper — accumulates member tags depth-first.
    void CollectMemberTags(
        const UJournalCollectionDefinition* Collection,
        TSet<FGameplayTag>& OutTags) const;

    // Recursive helper — accumulates progress depth-first.
    FJournalCollectionProgress ComputeProgress(
        const UJournalCollectionDefinition* Collection,
        const TSet<FGameplayTag>& AcquiredSet) const;

    // Tag → soft asset ref. Populated at Initialize().
    TMap<FGameplayTag, TSoftObjectPtr<UJournalEntryDataAsset>> EntryRegistry;

    // CollectionTag → loaded definition. Populated at Initialize().
    // Collections are small and fully loaded — no async needed.
    TMap<FGameplayTag, TObjectPtr<UJournalCollectionDefinition>> CollectionRegistry;

    FStreamableHandle EntryLoadHandle;
    FStreamableHandle CollectionLoadHandle;
};
```

---

## Supporting Types

```cpp
// JournalTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalCollectionProgress
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 Found = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Total = 0;

    float Ratio() const
    {
        return Total > 0 ? static_cast<float>(Found) / Total : 0.f;
    }

    bool IsComplete() const { return Found >= Total && Total > 0; }
};
```

---

## Key Method Implementations

### `Initialize`

```cpp
void UJournalRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadAllEntryAssets();
    LoadAllCollections();
}
```

### `LoadAllEntryAssets`

```cpp
void UJournalRegistrySubsystem::LoadAllEntryAssets()
{
    UAssetManager& AM = UAssetManager::Get();
    TArray<FPrimaryAssetId> AssetIds;
    AM.GetPrimaryAssetIdList(
        FPrimaryAssetType(TEXT("JournalEntry")), AssetIds);

    TArray<FSoftObjectPath> Paths;
    for (const FPrimaryAssetId& Id : AssetIds)
    {
        FSoftObjectPath Path;
        AM.GetPrimaryAssetPath(Id, Path);
        Paths.Add(Path);
    }

    // Load all entry assets synchronously at startup.
    // These are lightweight (no textures — textures are soft refs inside the asset).
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

    UE_LOG(LogJournal, Log, TEXT("Loaded %d journal entry assets."),
        EntryRegistry.Num());
}
```

> **Why sync load?** Entry assets are lightweight — they contain only tags, FText, and soft refs to heavy assets (textures). The heavy assets remain unloaded. The entry asset list is bounded by content, not player count, so sync load at startup is safe.

### `GetCollectionProgress`

```cpp
FJournalCollectionProgress UJournalRegistrySubsystem::GetCollectionProgress(
    FGameplayTag CollectionTag,
    const TSet<FGameplayTag>& AcquiredSet) const
{
    const TObjectPtr<UJournalCollectionDefinition>* DefPtr =
        CollectionRegistry.Find(CollectionTag);
    if (!DefPtr || !*DefPtr) return {};
    return ComputeProgress(*DefPtr, AcquiredSet);
}

FJournalCollectionProgress UJournalRegistrySubsystem::ComputeProgress(
    const UJournalCollectionDefinition* Collection,
    const TSet<FGameplayTag>& AcquiredSet) const
{
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

    for (const TSoftObjectPtr<UJournalCollectionDefinition>& SubRef
         : Collection->SubCollections)
    {
        if (const UJournalCollectionDefinition* Sub = SubRef.Get())
        {
            FJournalCollectionProgress SubProgress = ComputeProgress(Sub, AcquiredSet);
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
        CollectMemberTags(*DefPtr, Out);
    return Out;
}

void UJournalRegistrySubsystem::CollectMemberTags(
    const UJournalCollectionDefinition* Collection,
    TSet<FGameplayTag>& OutTags) const
{
    for (const TSoftObjectPtr<UJournalEntryDataAsset>& MemberRef : Collection->Members)
        if (const UJournalEntryDataAsset* Asset = MemberRef.Get())
            OutTags.Add(Asset->EntryTag);

    for (const TSoftObjectPtr<UJournalCollectionDefinition>& SubRef
         : Collection->SubCollections)
        if (const UJournalCollectionDefinition* Sub = SubRef.Get())
            CollectMemberTags(Sub, OutTags);
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
