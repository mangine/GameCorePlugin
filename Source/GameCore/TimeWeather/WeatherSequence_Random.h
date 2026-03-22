#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TimeWeather/WeatherSequence.h"
#include "WeatherSequence_Random.generated.h"

// =============================================================================
// FWeightedWeather
// =============================================================================

/**
 * One candidate entry in UWeatherSequence_Random.
 * Uses predecessor filtering to constrain which weathers can follow which.
 */
USTRUCT(BlueprintType)
struct FWeightedWeather
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly) FGameplayTag WeatherTag;

    /** Relative probability weight. Higher = more frequent. */
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f))
    float Weight = 1.f;

    /**
     * If non-empty, this weather can only follow one of these tags.
     * Empty = can follow any weather.
     * Predecessor constraints (not follower) keep new entries self-contained.
     */
    UPROPERTY(EditDefaultsOnly)
    TArray<FGameplayTag> ValidPredecessors;

    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f))
    float TransitionMinSeconds = 120.f;

    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f))
    float TransitionMaxSeconds = 300.f;
};

// =============================================================================
// UWeatherSequence_Random
// =============================================================================

/** Weighted random selection with optional predecessor filtering. */
UCLASS(BlueprintType)
class GAMECORE_API UWeatherSequence_Random : public UWeatherSequence
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Sequence")
    TArray<FWeightedWeather> Entries;

    UPROPERTY(EditDefaultsOnly, Category="Sequence")
    int32 KeyframesPerDay = 3;

    virtual FGameplayTag GetNextWeather(
        FGameplayTag          Current,
        const FSeasonContext& Context,
        FRandomStream&        Stream,
        float&                OutTransitionSeconds) override;

    virtual int32 GetKeyframesPerDay() const override { return KeyframesPerDay; }

private:
    /** Builds eligible subset: entries whose ValidPredecessors allow Current. */
    TArray<const FWeightedWeather*> GetEligibleEntries(FGameplayTag Current) const;

    /** Weighted random pick from eligible set using Stream. */
    const FWeightedWeather* PickWeighted(
        const TArray<const FWeightedWeather*>& Eligible,
        FRandomStream& Stream) const;
};
