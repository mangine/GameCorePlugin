// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Engine/StreamableManager.h"
#include "Journal/JournalTypes.h"
#include "JournalRegistrySubsystem.generated.h"

class UJournalEntryDataAsset;
class UJournalCollectionDefinition;

/**
 * UJournalRegistrySubsystem
 *
 * UGameInstanceSubsystem. Asset registry for all journal entry data assets
 * and collection definitions. Loaded once at game instance initialization.
 * Survives seamless travel.
 *
 * Responsibilities:
 * - Load all UJournalEntryDataAsset and UJournalCollectionDefinition assets at init
 * - O(1) tag → asset resolution via EntryRegistry
 * - GetCollectionProgress() for UI progress bars
 * - GetCollectionMemberTags() for UJournalComponent collection filtering
 * - GetEntryAsset() for on-demand content loading by UI
 */
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
