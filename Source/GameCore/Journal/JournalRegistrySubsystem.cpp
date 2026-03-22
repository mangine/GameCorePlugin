// Copyright GameCore Plugin. All Rights Reserved.
#include "Journal/JournalRegistrySubsystem.h"
#include "Journal/JournalEntryDataAsset.h"
#include "Journal/JournalCollectionDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

// ============================================================================
// Lifecycle
// ============================================================================

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

// ============================================================================
// Public API
// ============================================================================

TSoftObjectPtr<UJournalEntryDataAsset> UJournalRegistrySubsystem::GetEntryAsset(
    FGameplayTag EntryTag) const
{
    if (const TSoftObjectPtr<UJournalEntryDataAsset>* Found = EntryRegistry.Find(EntryTag))
        return *Found;
    return TSoftObjectPtr<UJournalEntryDataAsset>();
}

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

TArray<const UJournalCollectionDefinition*>
UJournalRegistrySubsystem::GetCollectionsForTrack(FGameplayTag TrackTag) const
{
    TArray<const UJournalCollectionDefinition*> Out;
    for (const auto& Pair : CollectionRegistry)
    {
        if (Pair.Value && Pair.Value->TrackTag == TrackTag)
            Out.Add(Pair.Value.Get());
    }
    return Out;
}

// ============================================================================
// Private — Asset Loading
// ============================================================================

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
    if (Paths.Num() > 0)
    {
        EntryLoadHandle = UAssetManager::Get().GetStreamableManager()
            .RequestSyncLoad(Paths);
    }

    EntryRegistry.Reserve(Paths.Num());
    for (const FSoftObjectPath& Path : Paths)
    {
        if (UJournalEntryDataAsset* Asset =
            Cast<UJournalEntryDataAsset>(Path.ResolveObject()))
        {
            if (Asset->EntryTag.IsValid())
            {
                EntryRegistry.Add(Asset->EntryTag,
                    TSoftObjectPtr<UJournalEntryDataAsset>(Path));
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("Journal: LoadAllEntryAssets: asset '%s' has no EntryTag — skipped."),
                    *Asset->GetName());
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Journal: loaded %d entry assets."),
        EntryRegistry.Num());
}

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

    if (Paths.Num() > 0)
    {
        CollectionLoadHandle = UAssetManager::Get().GetStreamableManager()
            .RequestSyncLoad(Paths);
    }

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

    UE_LOG(LogTemp, Log, TEXT("Journal: loaded %d collection definitions."),
        CollectionRegistry.Num());
}

// ============================================================================
// Private — Recursive Helpers
// ============================================================================

void UJournalRegistrySubsystem::CollectMemberTags(
    const UJournalCollectionDefinition* Collection,
    TSet<FGameplayTag>& OutTags,
    TSet<FGameplayTag>& VisitedCollections) const
{
    if (!Collection || VisitedCollections.Contains(Collection->CollectionTag)) return;
    VisitedCollections.Add(Collection->CollectionTag);

    for (const TSoftObjectPtr<UJournalEntryDataAsset>& MemberRef : Collection->Members)
    {
        if (const UJournalEntryDataAsset* Asset = MemberRef.Get())
            OutTags.Add(Asset->EntryTag);
    }

    for (const TSoftObjectPtr<UJournalCollectionDefinition>& SubRef
         : Collection->SubCollections)
    {
        if (const UJournalCollectionDefinition* Sub = SubRef.Get())
            CollectMemberTags(Sub, OutTags, VisitedCollections);
    }
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
