// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Journal/JournalEntry.h"
#include "JournalEntryDataAsset.generated.h"

/**
 * Abstract base for all journal entry data assets.
 * Concrete subclasses belong in the game module — not in GameCore.
 *
 * Asset Manager primary asset type: "JournalEntry"
 * All instances must be placed under the directory scanned by Asset Manager.
 */
UCLASS(Abstract, BlueprintType)
class GAMECORE_API UJournalEntryDataAsset
    : public UPrimaryDataAsset
    , public IJournalEntry
{
    GENERATED_BODY()
public:
    /**
     * Unique tag identifying this entry type.
     * - Must be unique per non-repeatable entry.
     * - Must match the EntryTag passed to UJournalComponent::AddEntry().
     * - Must be under the Journal.Entry namespace.
     * Validated in IsDataValid().
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Journal",
              meta = (Categories = "Journal.Entry"))
    FGameplayTag EntryTag;

    /**
     * Track this entry belongs to.
     * Must match the TrackTag passed to UJournalComponent::AddEntry().
     * e.g. Journal.Track.Books
     * Validated in IsDataValid().
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Journal",
              meta = (Categories = "Journal.Track"))
    FGameplayTag TrackTag;

    // IJournalEntry — GetTrackTag default implementation returns TrackTag.
    virtual FGameplayTag GetTrackTag_Implementation() const override { return TrackTag; }

    // GetEntryTitle has no default — concrete subclasses must implement.

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
    // Validates:
    //   - EntryTag is set (not empty)
    //   - TrackTag is set (not empty)
#endif

protected:
    // Override GetPrimaryAssetId to use EntryTag as the asset name portion.
    // This keeps Asset Manager IDs stable and human-readable.
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(
            FPrimaryAssetType(TEXT("JournalEntry")),
            EntryTag.IsValid() ? EntryTag.GetTagName() : GetFName());
    }
};
