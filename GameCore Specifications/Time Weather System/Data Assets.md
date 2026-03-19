# Data Assets

**Sub-page of:** [Time Weather System](../Time%20Weather%20System.md)

Covers `UTimeWeatherConfig`, `UWeatherContextAsset`, `USeasonDefinition`, and `UWeatherEventDefinition`.

---

## UTimeWeatherConfig

A `UDataAsset` assigned to `UTimeWeatherSubsystem` via `DefaultGame.ini` or a project settings object. Holds global deterministic time parameters.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UTimeWeatherConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    // Real Unix seconds that maps to in-game day 0.
    // Example: set to a January 1 midnight UTC to align seasons with real calendar.
    UPROPERTY(EditDefaultsOnly, Category="Time")
    int64 ServerEpochOffsetSeconds = 0;

    // Real seconds per in-game day. 1200 = 20-minute days.
    UPROPERTY(EditDefaultsOnly, Category="Time", Meta=(ClampMin=60.f))
    float DayDurationSeconds = 1200.f;

    // Total in-game days in a full year (all seasons combined).
    UPROPERTY(EditDefaultsOnly, Category="Time", Meta=(ClampMin=1))
    int32 YearLengthDays = 120;

    // Base seed mixed with day index and context ID to produce per-day weather seed.
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    int32 WeatherSeedBase = 42;

    // Global weather context. Used when no IWeatherContextProvider is supplied.
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    TObjectPtr<UWeatherContextAsset> GlobalContext;

    // Normalized day time [0,1) at which DawnBegan event fires.
    UPROPERTY(EditDefaultsOnly, Category="Events", Meta=(ClampMin=0.f, ClampMax=1.f))
    float DawnThreshold = 0.25f;

    // Normalized day time [0,1) at which DuskBegan event fires.
    UPROPERTY(EditDefaultsOnly, Category="Events", Meta=(ClampMin=0.f, ClampMax=1.f))
    float DuskThreshold = 0.75f;
};
```

**Note:** `ServerEpochOffsetSeconds` is the single lever to align in-game seasons with real-world calendar dates, or to offset them arbitrarily. A value of 0 with `YearLengthDays = 120` means day 0 of in-game history was the Unix epoch (1 Jan 1970), and seasons cycle every 120 days.

---

## UWeatherContextAsset

Full configuration for one weather region (or the global world). Referenced by `IWeatherContextProvider` implementations and by `UTimeWeatherConfig::GlobalContext`.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UWeatherContextAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    // Ordered list of seasons. Empty = no season cycling (permafrost, permanent night, etc.).
    // Seasons occupy equal slices of YearLengthDays unless DaysOverride > 0 on individual seasons.
    UPROPERTY(EditDefaultsOnly, Category="Seasons")
    TArray<TObjectPtr<USeasonDefinition>> Seasons;

    // Override: each season lasts exactly this many in-game days.
    // 0 = divide YearLengthDays equally across Seasons.
    UPROPERTY(EditDefaultsOnly, Category="Seasons", Meta=(ClampMin=0))
    int32 DaysPerSeason = 0;

    // Used when Seasons is empty or between seasons with no sequence.
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    TObjectPtr<UWeatherSequence> DefaultWeatherSequence;

    // Day-night curve used when Seasons is empty or current season has no override.
    // X = normalized day time [0,1), Y = daylight intensity [0,1].
    // A flat curve at Y=0 = permanent night.
    UPROPERTY(EditDefaultsOnly, Category="DayNight")
    TObjectPtr<UCurveFloat> DefaultDayNightCurve;

#if WITH_EDITORONLY_DATA
    UFUNCTION(CallInEditor, Category="Debug")
    void PreviewSeasonTimeline(); // draws a simple debug timeline in the editor
#endif
};
```

**Season duration computation:**
```cpp
// Called at subsystem init and on config change.
void UWeatherContextAsset::ComputeSeasonRanges(int32 YearLengthDays,
    TArray<FSeasonRange>& OutRanges) const
{
    if (Seasons.IsEmpty()) return;

    int32 PerSeason = (DaysPerSeason > 0)
        ? DaysPerSeason
        : FMath::Max(1, YearLengthDays / Seasons.Num());

    int32 Start = 0;
    for (USeasonDefinition* Season : Seasons)
    {
        FSeasonRange& R = OutRanges.AddDefaulted_GetRef();
        R.Season    = Season;
        R.StartDay  = Start;
        R.EndDay    = Start + PerSeason - 1;
        Start      += PerSeason;
    }
}
```

---

## USeasonDefinition

Defines one season's identity, day-night behaviour, weather preferences, and timed event chances.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API USeasonDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    // Tag used for external queries. E.g. Season.Summer, Season.Permafrost.
    UPROPERTY(EditDefaultsOnly, Category="Identity")
    FGameplayTag SeasonTag;

    // % of this season's duration that blends IN from the prior season. [0, 50].
    // E.g. 15 means the first 15% of summer overlaps with the end of spring.
    UPROPERTY(EditDefaultsOnly, Category="Transition", Meta=(ClampMin=0, ClampMax=50))
    float TransitionInPercent = 15.f;

    // Day-night curve for this season. Overrides UWeatherContextAsset::DefaultDayNightCurve.
    // X = normalized day time [0,1), Y = daylight [0,1].
    // Null = use context default.
    UPROPERTY(EditDefaultsOnly, Category="DayNight")
    TObjectPtr<UCurveFloat> DayNightCurve;

    // Weather sequence driving base weather selection for this season.
    // Null = use UWeatherContextAsset::DefaultWeatherSequence.
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    TObjectPtr<UWeatherSequence> WeatherSequence;

    // Probabilistic time-window overlay events for this season.
    // Evaluated once per day at dawn to build the daily schedule.
    UPROPERTY(EditDefaultsOnly, Category="Events")
    TArray<FSeasonWeatherEvent> TimedEvents;
};
```

### FSeasonWeatherEvent

```cpp
USTRUCT(BlueprintType)
struct FSeasonWeatherEvent
{
    GENERATED_BODY()

    // Event to trigger if the probability roll succeeds.
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UWeatherEventDefinition> Event;

    // Normalized day-time window in which this event can occur. [0,1).
    UPROPERTY(EditDefaultsOnly, Meta=(ClampMin=0.f, ClampMax=1.f))
    float WindowStart = 0.5f;  // e.g. 0.5 = noon

    UPROPERTY(EditDefaultsOnly, Meta=(ClampMin=0.f, ClampMax=1.f))
    float WindowEnd   = 0.75f; // e.g. 0.75 = late afternoon

    // Probability [0,1] that this event is scheduled on any given day.
    UPROPERTY(EditDefaultsOnly, Meta=(ClampMin=0.f, ClampMax=1.f))
    float DailyProbability = 0.3f;
};
```

**Season blend alpha computation:**
```cpp
// Called by subsystem each tick. Returns [0,1]: 1 = fully in this season.
float ComputeSeasonBlendAlpha(float DayWithinSeason, float SeasonDuration) const
{
    float TransitionDays = SeasonDuration * (TransitionInPercent / 100.f);
    if (TransitionDays < 1.f) return 1.f;
    return FMath::Clamp(DayWithinSeason / TransitionDays, 0.f, 1.f);
}
```

---

## UWeatherEventDefinition

Defines one overlay weather event: its weather tag, lifecycle durations, priority, and optionally a minimum base weather requirement.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UWeatherEventDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    // Weather tag applied as the overlay layer in FWeatherBlendState.
    UPROPERTY(EditDefaultsOnly, Category="Identity")
    FGameplayTag WeatherTag;

    // Higher value wins when two events compete for the overlay slot.
    // Equal priority: first-activated wins.
    UPROPERTY(EditDefaultsOnly, Category="Priority")
    int32 Priority = 0;

    // Real-time seconds to fade the overlay in (OverlayAlpha 0→1).
    UPROPERTY(EditDefaultsOnly, Category="Lifecycle", Meta=(ClampMin=0.f))
    float FadeInSeconds = 60.f;

    // Real-time seconds the overlay stays at full alpha after fade-in.
    UPROPERTY(EditDefaultsOnly, Category="Lifecycle", Meta=(ClampMin=0.f))
    float SustainSeconds = 300.f;

    // Real-time seconds to fade the overlay out (OverlayAlpha 1→0).
    UPROPERTY(EditDefaultsOnly, Category="Lifecycle", Meta=(ClampMin=0.f))
    float FadeOutSeconds = 60.f;

    float TotalDurationSeconds() const
    {
        return FadeInSeconds + SustainSeconds + FadeOutSeconds;
    }
};
```

**Note:** Event durations are in **real-time seconds**, not in-game time, so a 5-minute sustain is a 5-minute sustain regardless of day speed. This is intentional — storm durations should feel consistent to players regardless of day-duration config.
