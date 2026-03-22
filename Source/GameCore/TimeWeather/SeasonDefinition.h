#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "SeasonDefinition.generated.h"

class UWeatherSequence;
class UWeatherEventDefinition;

// =============================================================================
// FSeasonWeatherEvent
// =============================================================================

/**
 * Probabilistic timed overlay event that may be scheduled on any day in its owning season.
 * Evaluated once per day at rollover using a seeded random stream.
 */
USTRUCT(BlueprintType)
struct FSeasonWeatherEvent
{
    GENERATED_BODY()

    /** The overlay event to trigger if the probability roll succeeds. */
    UPROPERTY(EditDefaultsOnly)
    TObjectPtr<UWeatherEventDefinition> Event;

    /** Normalised day-time [0,1) window in which this event may start. */
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f, ClampMax=1.f))
    float WindowStart = 0.5f;

    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f, ClampMax=1.f))
    float WindowEnd = 0.75f;

    /** Probability [0,1] that this event is scheduled on any given day. */
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f, ClampMax=1.f))
    float DailyProbability = 0.3f;
};

// =============================================================================
// USeasonDefinition
// =============================================================================

/** Defines one season's identity, day-night behaviour, weather sequence, and timed event chances. */
UCLASS(BlueprintType)
class GAMECORE_API USeasonDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    /** Gameplay Tag for this season. Example: Season.Summer, Season.Winter. */
    UPROPERTY(EditDefaultsOnly, Category="Identity")
    FGameplayTag SeasonTag;

    /**
     * Percentage of this season's total duration that blends IN from the prior season.
     * Range [0, 50]. During this window, SeasonBlendAlpha in FWeatherBlendState rises 0→1.
     */
    UPROPERTY(EditDefaultsOnly, Category="Transition",
        meta=(ClampMin=0, ClampMax=50))
    float TransitionInPercent = 15.f;

    /**
     * Day-night intensity curve for this season.
     * X = normalised day time [0,1), Y = daylight [0,1].
     * Null = use UWeatherContextAsset::DefaultDayNightCurve.
     */
    UPROPERTY(EditDefaultsOnly, Category="DayNight")
    TObjectPtr<UCurveFloat> DayNightCurve;

    /**
     * Weather sequence for this season.
     * Null = use UWeatherContextAsset::DefaultWeatherSequence.
     */
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    TObjectPtr<UWeatherSequence> WeatherSequence;

    /** Time-windowed overlay events that may occur on any day in this season. */
    UPROPERTY(EditDefaultsOnly, Category="Events")
    TArray<FSeasonWeatherEvent> TimedEvents;

    /**
     * Computes the season blend-in alpha given days into this season.
     * Returns 1.0 if TransitionInPercent is 0 (no transition).
     */
    float ComputeSeasonBlendAlpha(float DayWithinSeason, float SeasonDurationDays) const
    {
        if (TransitionInPercent <= 0.f || SeasonDurationDays <= 0.f)
            return 1.f;
        const float TransitionDays =
            SeasonDurationDays * (TransitionInPercent / 100.f);
        return FMath::Clamp(
            DayWithinSeason / FMath::Max(1.f, TransitionDays), 0.f, 1.f);
    }
};
