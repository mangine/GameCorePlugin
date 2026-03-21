# SeasonDefinition

**Files:** `SeasonDefinition.h/.cpp`  
**Part of:** Time Weather System

---

## USeasonDefinition

Defines one season's identity, day-night behaviour, weather sequence, and timed event chances.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API USeasonDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    // Gameplay Tag for this season. Used by external systems to query the current season.
    // Example: Season.Summer, Season.Winter, Season.Permafrost.
    UPROPERTY(EditDefaultsOnly, Category="Identity")
    FGameplayTag SeasonTag;

    // Percentage of this season's total duration that blends IN from the prior season.
    // Range [0, 50]. During this window, SeasonBlendAlpha in FWeatherBlendState rises
    // from 0 to 1. Example: 15 means the first 15% of Summer overlaps Spring tail.
    UPROPERTY(EditDefaultsOnly, Category="Transition",
        meta=(ClampMin=0, ClampMax=50))
    float TransitionInPercent = 15.f;

    // Day-night intensity curve for this season.
    // Overrides UWeatherContextAsset::DefaultDayNightCurve.
    // Null = use context default.
    UPROPERTY(EditDefaultsOnly, Category="DayNight")
    TObjectPtr<UCurveFloat> DayNightCurve;

    // Weather sequence for this season.
    // Overrides UWeatherContextAsset::DefaultWeatherSequence.
    // Null = use context default.
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    TObjectPtr<UWeatherSequence> WeatherSequence;

    // Time-windowed overlay events that may occur on any day in this season.
    // Evaluated once per day at rollover using a seeded random stream.
    UPROPERTY(EditDefaultsOnly, Category="Events")
    TArray<FSeasonWeatherEvent> TimedEvents;

    // Computes the season blend-in alpha given days into this season.
    // Returns 1.0 if TransitionInPercent is 0 (no transition).
    float ComputeSeasonBlendAlpha(float DayWithinSeason, float SeasonDurationDays) const
    {
        if (TransitionInPercent <= 0.f || SeasonDurationDays <= 0.f)
            return 1.f;
        const float TransitionDays = SeasonDurationDays * (TransitionInPercent / 100.f);
        return FMath::Clamp(DayWithinSeason / FMath::Max(1.f, TransitionDays), 0.f, 1.f);
    }
};
```

---

## FSeasonWeatherEvent

Defines a probabilistic timed overlay event that may be scheduled on any day in its owning season.

```cpp
USTRUCT(BlueprintType)
struct FSeasonWeatherEvent
{
    GENERATED_BODY()

    // The overlay event to trigger if the probability roll succeeds.
    UPROPERTY(EditDefaultsOnly)
    TObjectPtr<UWeatherEventDefinition> Event;

    // Normalised day-time [0,1) window in which this event may start.
    // Example: WindowStart=0.5, WindowEnd=0.75 = noon to late afternoon.
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f, ClampMax=1.f))
    float WindowStart = 0.5f;

    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f, ClampMax=1.f))
    float WindowEnd = 0.75f;

    // Probability [0,1] that this event is scheduled on any given day.
    // Rolled once per day per context using the day's seeded FRandomStream.
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f, ClampMax=1.f))
    float DailyProbability = 0.3f;
};
```
