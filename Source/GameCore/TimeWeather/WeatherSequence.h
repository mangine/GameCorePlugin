#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TimeWeather/TimeWeatherTypes.h"
#include "WeatherSequence.generated.h"

/**
 * Abstract base for weather selection strategies.
 * Called once per keyframe slot during daily schedule construction at dawn.
 * Never called per-tick. Must be stateless and deterministic.
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, Blueprintable)
class GAMECORE_API UWeatherSequence : public UObject
{
    GENERATED_BODY()
public:
    /**
     * Returns the next weather tag and the real-time blend transition duration.
     *
     * Called once per keyframe slot at dawn. Must be deterministic given the same Seed.
     * Must NOT access world state, components, or subsystems.
     *
     * @param CurrentWeather       Tag of the ending weather (invalid = first call of day).
     * @param Context              Season and time context at call site.
     * @param Stream               Seeded random stream. Use this exclusively.
     * @param OutTransitionSeconds Set to real-time seconds for the blend transition.
     * @return Tag of the next weather state.
     */
    virtual FGameplayTag GetNextWeather(
        FGameplayTag          CurrentWeather,
        const FSeasonContext& Context,
        FRandomStream&        Stream,
        float&                OutTransitionSeconds)
        PURE_VIRTUAL(GetNextWeather, return FGameplayTag(););

    /**
     * How many weather keyframes to generate per day.
     * Default: 3 (morning / afternoon / evening).
     * Override to 1 for a stable all-day weather context.
     */
    virtual int32 GetKeyframesPerDay() const { return 3; }
};
