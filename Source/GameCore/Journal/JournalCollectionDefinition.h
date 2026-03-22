// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "JournalCollectionDefinition.generated.h"

class UJournalEntryDataAsset;

/**
 * Designer-authored DataAsset defining a named set of collectable journal entries.
 * Progress (found vs total) is always derived at runtime from the player's
 * ClientAcquiredSet — never stored per player.
 *
 * Supports arbitrary nesting via SubCollections.
 * Circular references are caught in IsDataValid().
 */
UCLASS(BlueprintType)
class GAMECORE_API UJournalCollectionDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    /**
     * Unique tag for this collection.
     * Used by UJournalRegistrySubsystem as the registry key.
     * Must be under Journal.Collection namespace.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Collection",
              meta = (Categories = "Journal.Collection"))
    FGameplayTag CollectionTag;

    /** Localized display name shown in UI collection tabs. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Collection")
    FText CollectionName;

    /**
     * Track this collection belongs to.
     * Used to filter collections per journal tab.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Collection",
              meta = (Categories = "Journal.Track"))
    FGameplayTag TrackTag;

    /**
     * All possible entries in this collection.
     * The asset picker is filtered to UJournalEntryDataAsset subclasses.
     * Progress = entries in this array whose EntryTag is in the player's AcquiredSet.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Collection",
              meta = (MustImplement = "/Script/GameCore.JournalEntry"))
    TArray<TSoftObjectPtr<UJournalEntryDataAsset>> Members;

    /**
     * Optional nested sub-collections.
     * Progress of this collection = Members + union of all SubCollections (recursive).
     * MUST NOT form a cycle — validated in IsDataValid().
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Collection")
    TArray<TSoftObjectPtr<UJournalCollectionDefinition>> SubCollections;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
    // Validates:
    //   - CollectionTag is set
    //   - TrackTag is set
    //   - No circular sub-collection references (via visited-set DFS)
#endif

protected:
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(
            FPrimaryAssetType(TEXT("JournalCollection")),
            CollectionTag.IsValid() ? CollectionTag.GetTagName() : GetFName());
    }
};
