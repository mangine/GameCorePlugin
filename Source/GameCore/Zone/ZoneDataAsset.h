#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ZoneDataAsset.generated.h"

/**
 * Static, authored-in-editor zone data. Never mutated at runtime.
 * Game modules subclass this to add domain-specific fields (tax rates, danger level, etc.).
 * The Zone System never inspects subclass fields.
 */
UCLASS(BlueprintType)
class GAMECORE_API UZoneDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /**
     * Unique identity tag, e.g. Zone.BlackpearlBay.
     * Used by UZoneSubsystem::GetZoneByName(). Expected to be unique per zone.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FGameplayTag ZoneNameTag;

    /**
     * Type taxonomy defined by the game module, e.g. Zone.Type.Territory.
     * Used for GetZonesByType() and type-filtered event listeners.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FGameplayTag ZoneTypeTag;

    /** Arbitrary static tags available to systems querying this zone. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FGameplayTagContainer StaticGameplayTags;

    /** Localised display name for UI. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FText DisplayName;

    /**
     * Priority for overlap resolution.
     * Higher value = evaluated first in QueryZonesAtPoint results.
     * Does not affect containment logic.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    int32 Priority = 0;
};
