# UJournalRegistrySubsystem

**Files:** `GameCore/Source/GameCore/Journal/JournalRegistrySubsystem.h` / `.cpp`  
**Type:** `UGameInstanceSubsystem`  
**Lives on:** Both server and client (game instance lifetime).  
**Survives seamless travel:** Yes — `UGameInstanceSubsystem` persists across level transitions.

---

## Responsibilities

- Load all `UJournalEntryDataAsset` and `UJournalCollectionDefinition` assets at game instance initialization via Asset Manager
- Maintain `TMap<FGameplayTag, TSoftObjectPtr<UJournalEntryDataAsset>>` for O(1) tag → asset resolution
- Maintain `TMap<FGameplayTag, TObjectPtr<UJournalCollectionDefinition>>` for collection access
- Provide `GetCollectionProgress()` for UI progress bars
- Provide `GetCollectionMemberTags()` for `UJournalComponent::GetPage()` collection filtering
- Provide `GetEntryAsset()` for on-demand content loading by UI

---

## Class Definition

```cpp
// JournalRegistrySubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Engine/StreamableManager.h"
#include "Journal/JournalTypes.h"
#include "JournalRegistrySubsystem.generated.h"

class UJournalEntryDataAsset;
class UJournalCollectionDefinition;

UCLASS()
class GAMECORE_API UJournalRegistrySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * Returns the soft asset reference for the given EntryTag.
     * Returns a null TSoftObjectPtr if not registered.
     * The returned asset is already loaded (sync load at init) — calling .Get() is safe.
     */
    TSoftObjectPtr<UJournalEntryDataAsset> GetEntryAsset(FGameplayTag EntryTag) const;

    /**
     * Computes collection progress recursively.
     * AcquiredSet should be UJournalComponent::GetClientAcquiredSet().
     * Returns a zero-progress struct if the collection is not found.
     */
    FJournalCollectionProgress GetCollectionProgress(
        FGameplayTag CollectionTag,
        const TSet<FGameplayTag>& AcquiredSet) const;

    /**
     * Returns the flat set of all EntryTags in a collection (recursive).
     * Used by UJournalComponent::GetFiltered() for collection filtering.
     * Includes members of all nested sub-collections.
     */
    TSet<FGameplayTag> GetCollectionMemberTags(FGameplayTag CollectionTag) const;

    /**
     * Returns all loaded collection definitions for a given track.
     * Used by UI to list collections per tab.
     */
    TArray<const UJournalCollectionDefinition*> GetCollectionsForTrack(
        FGameplayTag TrackTag) const;

private:
    void LoadAllEntryAssets();
    void LoadAllCollections();

    // Recursive depth-first accumulation of member tags.
    // VisitedCollections guards against circular references at runtime
    // (as a safety net — IsDataValid should catch them at author time).
    void CollectMemberTags(
        const UJournalCollectionDefinition* Collection,
        TSet<FGameplayTag>& OutTags,
        TSet<FGameplayTag>& VisitedCollections) const;

    // Recursive depth-first progress computation.
    FJournalCollectionProgress ComputeProgress(
        const UJournalCollectionDefinition* Collection,
        const TSet<FGameplayTag>& AcquiredSet,
        TSet<FGameplayTag>& VisitedCollections) const;

    // Tag → soft asset ref. Populated at Initialize().
    TMap<FGameplayTag, TSoftObjectPtr<UJournalEntryDataAsset>> EntryRegistry;

    // CollectionTag → loaded definition. Collections are fully loaded — no async needed.
    TMap<FGameplayTag, TObjectPtr<UJournalCollectionDefinition>> CollectionRegistry;

    // Keeps the entry and collection assets alive in memory.
    TSharedPtr<FStreamableHandle> EntryLoadHandle;
    TSharedPtr<FStreamableHandle> CollectionLoadHandle;
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

    // Sync load: entry assets are lightweight (tags + FText + soft refs only).
    // Heavy assets (textures, quest definitions) remain unloaded until BuildDetails.
    EntryLoadHandle = MakeShareable(
        UAssetManager::Get().GetStreamableManager()
            .RequestSyncLoad(Paths).Get());

    EntryRegistry.Reserve(Paths.Num());
    for (const FSoftObjectPath& Path : Paths)
    {
        if (UJournalEntryDataAsset* Asset =
            Cast<UJournalEntryDataAsset>(Path.ResolveObject()))
        {
            if (Asset->EntryTag.IsValid())
                EntryRegistry.Add(Asset->EntryTag,
                    TSoftObjectPtr<UJournalEntryDataAsset>(Path));
            else
                UE_LOG(LogJournal, Warning,
                    TEXT("LoadAllEntryAssets: asset '%s' has no EntryTag — skipped."),
                    *Asset->GetName());
        }
    }

    UE_LOG(LogJournal, Log, TEXT("Journal: loaded %d entry assets."),
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

    CollectionLoadHandle = MakeShareable(
        UAssetManager::Get().GetStreamableManager()
            .RequestSyncLoad(Paths).Get());

    CollectionRegistry.Reserve(Paths.Num());
    for (const FSoftObjectPath& Path : Paths)
    {
        if (UJournalCollectionDefinition* Def =
            Cast<UJournalCollectionDefinition>(Path.ResolveObject()))
        {
            if (Def->CollectionTag.IsValid())
                CollectionRegistry.Add(Def->CollectionTag, Def);
        }
    }

    UE_LOG(LogJournal, Log, TEXT("Journal: loaded %d collection definitions."),
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
    if (!Collection || Visited.Contains(Collection->CollectionTag)) return {};
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

    for (const TSoftObjectPtr<UJournalCollectionDefinition>& SubRef
         : Collection->SubCollections)
    {
        if (const UJournalCollectionDefinition* Sub = SubRef.Get())
        {
            FJournalCollectionProgress SubProg = ComputeProgress(Sub, AcquiredSet, Visited);
            Result.Found += SubProg.Found;
            Result.Total += SubProg.Total;
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
    TSet<FGameplayTag>& VisitedCollections) const
{
    if (!Collection || VisitedCollections.Contains(Collection->CollectionTag)) return;
    VisitedCollections.Add(Collection->CollectionTag);

    for (const TSoftObjectPtr<UJournalEntryDataAsset>& MemberRef : Collection->Members)
        if (const UJournalEntryDataAsset* Asset = MemberRef.Get())
            OutTags.Add(Asset->EntryTag);

    for (const TSoftObjectPtr<UJournalCollectionDefinition>& SubRef
         : Collection->SubCollections)
        if (const UJournalCollectionDefinition* Sub = SubRef.Get())
            CollectMemberTags(Sub, OutTags, VisitedCollections);
}
```

### `GetCollectionsForTrack`

```cpp
TArray<const UJournalCollectionDefinition*>
UJournalRegistrySubsystem::GetCollectionsForTrack(FGameplayTag TrackTag) const
{
    TArray<const UJournalCollectionDefinition*> Out;
    for (const auto& Pair : CollectionRegistry)
        if (Pair.Value && Pair.Value->TrackTag == TrackTag)
            Out.Add(Pair.Value.Get());
    return Out;
}
```

---

## Asset Manager Configuration (DefaultGame.ini)

```ini
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

> **Why `UGameInstanceSubsystem` and not `UWorldSubsystem`?** Entry and collection assets are game-instance-level data — they don't change per world. A `UGameInstanceSubsystem` avoids redundant reloading across seamless travel and keeps the registry alive for the entire session.
