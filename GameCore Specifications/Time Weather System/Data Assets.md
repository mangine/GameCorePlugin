# Data Assets

**Sub-page of:** [Time Weather System](../Time%20Weather%20System.md)

Covers `UTimeWeatherProjectSettings`, `UTimeWeatherConfig`, `UWeatherContextAsset`, `USeasonDefinition`, and `UWeatherEventDefinition`.

---

## UTimeWeatherProjectSettings

A `UDeveloperSettings` subclass. Appears in **Project Settings → Plugins → Time Weather**. This is the bridge between the editor config and the subsystem — the subsystem calls `GetDefault<UTimeWeatherProjectSettings>()` in `Initialize` to load the config asset.

```cpp
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Time Weather"))
class GAMECORE_API UTimeWeatherProjectSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UTimeWeatherProjectSettings()
    {
        CategoryName = TEXT("GameCore");
        SectionName  = TEXT("Time Weather");
    }

    // The config asset driving the entire Time Weather system.
    // Must be set before PIE or server startup.
    UPROPERTY(Config, EditAnywhere, Category="Config",
        meta=(AllowedClasses="TimeWeatherConfig"))
    TSoftObjectPtr<UTimeWeatherConfig> TimeWeatherConfig;

    // Override: settings live in DefaultGame.ini under [/Script/GameCore.TimeWeatherProjectSettings]
    virtual FName GetContainerName() const override { return TEXT("Project"); }
    virtual FName GetCategoryName()  const override { return TEXT("GameCore"); }
    virtual FName GetSectionName()   const override { return TEXT("TimeWeather"); }
};
```

**DefaultGame.ini entry:**
```ini
[/Script/GameCore.TimeWeatherProjectSettings]
TimeWeatherConfig=/Game/Data/TimeWeather/DA_TimeWeatherConfig.DA_TimeWeatherConfig
```

---

## UTimeWeatherConfig

A `UDataAsset` holding all global time and weather parameters. Referenced by `UTimeWeatherProjectSettings`. Contains no per-frame logic.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UTimeWeatherConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    // Real Unix seconds mapped to in-game day 0.
    // Example: 1735689600 = 2025-01-01 00:00:00 UTC aligns in-game year start
    // to a real calendar date. Set to 0 to start from the Unix epoch.
    UPROPERTY(EditDefaultsOnly, Category="Time")
    int64 ServerEpochOffsetSeconds = 0;

    // Real seconds per in-game day. Minimum 60 (enforced by ClampMin).
    // Common values: 1200 (20 min), 2400 (40 min), 4800 (80 min).
    UPROPERTY(EditDefaultsOnly, Category="Time", meta=(ClampMin=60.f))
    float DayDurationSeconds = 1200.f;

    // Total in-game days that make up one full year (all seasons combined).
    // Example: 4 seasons × 30 days each = 120.
    UPROPERTY(EditDefaultsOnly, Category="Time", meta=(ClampMin=1))
    int32 YearLengthDays = 120;

    // Fixed integer mixed with the day index and context GUID to produce
    // a per-day, per-context weather seed. Change this to globally reseed all weather.
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    int32 WeatherSeedBase = 42;

    // The global weather context used when no IWeatherContextProvider is registered.
    // Must not be null — the subsystem will log an error and disable itself if unset.
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    TObjectPtr<UWeatherContextAsset> GlobalContext;

    // Normalized day time [0,1) at which the DawnBegan GMS event fires.
    // Fires once per day per context. Default 0.25 = quarter-day (6am equivalent).
    UPROPERTY(EditDefaultsOnly, Category="Events",
        meta=(ClampMin=0.f, ClampMax=1.f))
    float DawnThreshold = 0.25f;

    // Normalized day time [0,1) at which the DuskBegan GMS event fires.
    // Default 0.75 = three-quarter-day (6pm equivalent).
    UPROPERTY(EditDefaultsOnly, Category="Events",
        meta=(ClampMin=0.f, ClampMax=1.f))
    float DuskThreshold = 0.75f;

    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override
    {
        EDataValidationResult Result = Super::IsDataValid(Context);
        if (!GlobalContext)
        {
            Context.AddError(FText::FromString(
                TEXT("UTimeWeatherConfig: GlobalContext must be set.")));
            Result = EDataValidationResult::Invalid;
        }
        if (DawnThreshold >= DuskThreshold)
        {
            Context.AddError(FText::FromString(
                TEXT("UTimeWeatherConfig: DawnThreshold must be less than DuskThreshold.")));
            Result = EDataValidationResult::Invalid;
        }
        return Result;
    }
};
```

---

## UWeatherContextAsset

Full weather configuration for one context (global world or a region). Referenced by `UTimeWeatherConfig::GlobalContext` and by region actors via `IWeatherContextProvider::GetWeatherContext()`.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UWeatherContextAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    // Ordered season list. Seasons are evaluated cyclically against day-of-year.
    // Empty = no season cycling. The context uses DefaultWeatherSequence
    // and DefaultDayNightCurve at all times.
    UPROPERTY(EditDefaultsOnly, Category="Seasons")
    TArray<TObjectPtr<USeasonDefinition>> Seasons;

    // Fixed day count per season. All seasons get the same allocation.
    // 0 = divide YearLengthDays equally. Remainder days are added to the last season.
    UPROPERTY(EditDefaultsOnly, Category="Seasons", meta=(ClampMin=0))
    int32 DaysPerSeason = 0;

    // Used when Seasons is empty, or as fallback if a season has no WeatherSequence set.
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    TObjectPtr<UWeatherSequence> DefaultWeatherSequence;

    // Day-night intensity curve. X = normalized day time [0,1), Y = daylight [0,1].
    // Used when Seasons is empty, or as fallback if the active season has no DayNightCurve.
    // A flat curve at Y=0.0 = permanent night. A flat curve at Y=1.0 = permanent day.
    UPROPERTY(EditDefaultsOnly, Category="DayNight")
    TObjectPtr<UCurveFloat> DefaultDayNightCurve;

    // Computes the ordered season ranges for a given year length.
    // Called once at subsystem initialisation and cached. Not called per-tick.
    void ComputeSeasonRanges(int32 YearLengthDays,
        TArray<FSeasonRange>& OutRanges) const
    {
        OutRanges.Reset();
        if (Seasons.IsEmpty()) return;

        const int32 Count     = Seasons.Num();
        const int32 PerSeason = (DaysPerSeason > 0)
            ? DaysPerSeason
            : FMath::Max(1, YearLengthDays / Count);

        int32 Start = 0;
        for (int32 i = 0; i < Count; ++i)
        {
            FSeasonRange& R = OutRanges.AddDefaulted_GetRef();
            R.Season   = Seasons[i];
            R.StartDay = Start;
            // Last season absorbs any remainder from integer division.
            R.EndDay   = (i == Count - 1)
                ? (YearLengthDays - 1)
                : (Start + PerSeason - 1);
            Start     += PerSeason;
        }
    }

    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override
    {
        EDataValidationResult Result = Super::IsDataValid(Context);
        if (!DefaultWeatherSequence && Seasons.IsEmpty())
        {
            Context.AddError(FText::FromString(
                TEXT("UWeatherContextAsset: DefaultWeatherSequence must be set when Seasons is empty.")));
            Result = EDataValidationResult::Invalid;
        }
        return Result;
    }
};
```

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

    // % of this season's total duration that blends IN from the prior season. Range [0, 50].
    // During this window, SeasonBlendAlpha in FWeatherBlendState rises from 0 to 1.
    // Example: 15 means the first 15% of Summer overlaps with the tail of Spring.
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

    // Computes the season blend-in alpha given how many days into this season we are.
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

### FSeasonWeatherEvent

Defines a probabilistic timed overlay event that may be scheduled on any day in its owning season.

```cpp
USTRUCT(BlueprintType)
struct FSeasonWeatherEvent
{
    GENERATED_BODY()

    // The overlay event to trigger if the probability roll succeeds.
    UPROPERTY(EditDefaultsOnly)
    TObjectPtr<UWeatherEventDefinition> Event;

    // Normalized day-time [0,1) window in which this event starts.
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

**RollTimedEvents logic** (called from `RebuildDailySchedule` for each season event):
```cpp
// Called after keyframe construction. Appends any successful event rolls
// as additional scheduled activations stored in FContextState.
void UTimeWeatherSubsystem::RollTimedEvents(
    FContextState& State,
    const FSeasonContext& SeasonCtx,
    FRandomStream& Stream,
    int32 Day)
{
    USeasonDefinition* Season = ResolveActiveSeason(State.Context, SeasonCtx);
    if (!Season) return;

    for (const FSeasonWeatherEvent& SE : Season->TimedEvents)
    {
        if (!SE.Event) continue;
        if (Stream.GetFraction() > SE.DailyProbability) continue;

        // Pick a random start time within the window.
        float StartNorm = Stream.FRandRange(SE.WindowStart, SE.WindowEnd);

        // Convert to a real-time wall-clock offset from day start.
        double DayStartSeconds = ((double)Day * (double)State.Context /* resolved DayDurationSeconds */);
        // Actual trigger is scheduled as a real-time offset stored in FContextState::ScheduledEventTriggers.
        FScheduledEventTrigger& Trigger = State.ScheduledEventTriggers.AddDefaulted_GetRef();
        Trigger.Event         = SE.Event;
        Trigger.TriggerNormTime = StartNorm;  // checked each tick against current NormDayTime
        Trigger.bFired        = false;
    }
}
```

See `UTimeWeatherSubsystem` → `FContextState::ScheduledEventTriggers` for the matching tick path.

---

## UWeatherEventDefinition

Defines one overlay weather event: its tag, lifecycle durations, and priority.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UWeatherEventDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    // Gameplay Tag applied as the overlay in FWeatherBlendState::OverlayWeather.
    // Also registered into the active event registry under this tag.
    UPROPERTY(EditDefaultsOnly, Category="Identity")
    FGameplayTag WeatherTag;

    // Higher value wins when two events compete for the overlay slot on the same context.
    // Equal priority: first-activated wins; the second is queued.
    UPROPERTY(EditDefaultsOnly, Category="Priority")
    int32 Priority = 0;

    // Real-time seconds to fade overlay in (OverlayAlpha 0→1).
    UPROPERTY(EditDefaultsOnly, Category="Lifecycle", meta=(ClampMin=0.f))
    float FadeInSeconds = 60.f;

    // Real-time seconds the overlay holds at full alpha after fade-in.
    UPROPERTY(EditDefaultsOnly, Category="Lifecycle", meta=(ClampMin=0.f))
    float SustainSeconds = 300.f;

    // Real-time seconds to fade overlay out (OverlayAlpha 1→0).
    UPROPERTY(EditDefaultsOnly, Category="Lifecycle", meta=(ClampMin=0.f))
    float FadeOutSeconds = 60.f;

    float TotalDurationSeconds() const
    {
        return FadeInSeconds + SustainSeconds + FadeOutSeconds;
    }

    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override
    {
        EDataValidationResult Result = Super::IsDataValid(Context);
        if (!WeatherTag.IsValid())
        {
            Context.AddError(FText::FromString(
                TEXT("UWeatherEventDefinition: WeatherTag must be set.")));
            Result = EDataValidationResult::Invalid;
        }
        return Result;
    }
};
```

**Note on event durations:** All durations are in **real-time seconds**, not in-game time. A 5-minute storm sustain is 5 minutes for players regardless of how fast the in-game day moves. This is intentional and should not be changed.
