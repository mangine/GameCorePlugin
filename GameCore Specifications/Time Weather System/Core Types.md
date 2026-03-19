# Core Types

**Sub-page of:** [Time Weather System](../Time%20Weather%20System.md)

All structs and interfaces declared in `TimeWeatherTypes.h` and `WeatherContextProvider.h`.

---

## FGameTimeSnapshot

Replicated once to clients on connect and on day rollover. Stateless config — any holder can derive current time without callbacks.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGameTimeSnapshot
{
    GENERATED_BODY()

    // Real Unix seconds aligned to in-game day 0.
    // Set in UTimeWeatherConfig::ServerEpochOffsetSeconds.
    UPROPERTY() int64  ServerEpochOffsetSeconds = 0;

    // Real seconds per in-game day. E.g. 1200 = 20-minute days.
    UPROPERTY() float  DayDurationSeconds       = 1200.f;

    // In-game days per full year (season cycle).
    UPROPERTY() int32  YearLengthDays           = 120;

    // --- Derived helpers (not replicated, computed locally) ---

    // [0, 1) position within the current day. 0 = midnight, 0.5 = noon.
    float GetNormalizedDayTime(int64 NowUnixSeconds) const
    {
        double Elapsed = (double)(NowUnixSeconds - ServerEpochOffsetSeconds);
        double DayFrac = FMath::Fmod(Elapsed / DayDurationSeconds, 1.0);
        return (float)FMath::Max(0.0, DayFrac);
    }

    // Absolute in-game day index since epoch.
    int32 GetCurrentDay(int64 NowUnixSeconds) const
    {
        double Elapsed = (double)(NowUnixSeconds - ServerEpochOffsetSeconds);
        return (int32)FMath::Max(0.0, FMath::FloorToDouble(Elapsed / DayDurationSeconds));
    }

    // Day index within the current year [0, YearLengthDays).
    int32 GetDayOfYear(int64 NowUnixSeconds) const
    {
        return GetCurrentDay(NowUnixSeconds) % FMath::Max(1, YearLengthDays);
    }
};
```

**Note:** Clients hold this snapshot on `AGameState` (replicated property). They call `GetNormalizedDayTime(FPlatformTime::Seconds())` locally for display — no server round-trips.

---

## FSeasonContext

Passed into `UWeatherSequence::GetNextWeather` and stored per context state. Provides sequence authors with full situational data.

```cpp
USTRUCT()
struct FSeasonContext
{
    GENERATED_BODY()

    float        NormalizedDayTime   = 0.f;   // [0,1)
    FGameplayTag CurrentSeason;                // invalid = no season
    FGameplayTag PreviousSeason;               // invalid = none
    float        SeasonBlendAlpha    = 1.f;   // 1 = fully in current season
    int32        CurrentDay          = 0;
    FGameplayTag LastWeather;                  // tag of most recently resolved base weather
};
```

---

## FWeatherBlendState

The sole output of this system. Downstream consumers (VFX, audio, spawners) only ever touch this struct.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FWeatherBlendState
{
    GENERATED_BODY()

    // Base layer: lerp from A to B over BlendAlpha.
    // When BlendAlpha == 0: pure A. BlendAlpha == 1: pure B.
    // After a transition completes, A = B and BlendAlpha = 0.
    UPROPERTY(BlueprintReadOnly) FGameplayTag BaseWeatherA;
    UPROPERTY(BlueprintReadOnly) FGameplayTag BaseWeatherB;
    UPROPERTY(BlueprintReadOnly) float        BlendAlpha    = 0.f;

    // Overlay layer: an active event weather fades in/out independently.
    // Invalid tag = no overlay active.
    UPROPERTY(BlueprintReadOnly) FGameplayTag OverlayWeather;
    UPROPERTY(BlueprintReadOnly) float        OverlayAlpha  = 0.f;

    // Season context for consumers that need it (e.g. foliage, NPC schedules).
    UPROPERTY(BlueprintReadOnly) FGameplayTag CurrentSeason;
    UPROPERTY(BlueprintReadOnly) FGameplayTag PreviousSeason;
    UPROPERTY(BlueprintReadOnly) float        SeasonBlendAlpha = 1.f;

    bool IsValid() const { return BaseWeatherA.IsValid(); }
};
```

---

## FDailyWeatherKeyframe

A single entry in the day's weather schedule. The subsystem builds an ordered array of these at dawn.

```cpp
USTRUCT()
struct FDailyWeatherKeyframe
{
    GENERATED_BODY()

    float        StartNormTime   = 0.f;   // [0,1) within the day when this weather begins
    float        EndNormTime     = 1.f;   // [0,1) when the next begins (or day ends)
    FGameplayTag WeatherTag;               // target base weather
    float        TransitionDuration = 0.f; // real seconds for blend alpha 0→1
};
```

## FDailyWeatherSchedule

```cpp
USTRUCT()
struct FDailyWeatherSchedule
{
    GENERATED_BODY()

    TArray<FDailyWeatherKeyframe> Keyframes;  // sorted by StartNormTime
    int32  CurrentKeyframeIndex = 0;

    // Returns the keyframe active at NormTime.
    const FDailyWeatherKeyframe* GetActiveKeyframe(float NormTime) const;
};
```

---

## FActiveWeatherEvent

Represents an in-flight overlay event with full lifecycle tracking.

```cpp
USTRUCT()
struct FActiveWeatherEvent
{
    GENERATED_BODY()

    FGuid                    EventId;           // cancellation handle
    UWeatherEventDefinition* Definition = nullptr;
    int32                    Priority   = 0;    // copied from Definition at activation

    // Wall-clock timestamps (FPlatformTime::Seconds())
    double StartTime    = 0.0;
    double FadeInEnd    = 0.0;
    double SustainEnd   = 0.0;
    double FadeOutEnd   = 0.0;

    float  CurrentAlpha = 0.f;   // updated each tick: 0→1 (fade-in), 1 (sustain), 1→0 (fade-out)

    bool IsExpired(double NowSeconds) const { return NowSeconds >= FadeOutEnd; }

    // Recompute CurrentAlpha from NowSeconds. Call once per tick.
    void TickAlpha(double NowSeconds);
};

// Implementation:
void FActiveWeatherEvent::TickAlpha(double Now)
{
    if (Now < FadeInEnd)
        CurrentAlpha = (float)FMath::GetMappedRangeValueClamped(
            FVector2D(StartTime, FadeInEnd), FVector2D(0.0, 1.0), Now);
    else if (Now < SustainEnd)
        CurrentAlpha = 1.f;
    else
        CurrentAlpha = (float)FMath::GetMappedRangeValueClamped(
            FVector2D(SustainEnd, FadeOutEnd), FVector2D(1.0, 0.0), Now);
}
```

---

## FRegionWeatherBlend

Future integration point for area-boundary weather blending. The area system drives `SpatialAlpha`; this system exposes a helper to lerp two blend states.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRegionWeatherBlend
{
    GENERATED_BODY()

    FWeatherBlendState StateA;          // weather of region A
    FWeatherBlendState StateB;          // weather of region B
    float              SpatialAlpha;    // 0 = pure A, 1 = pure B — driven by area system
};
```

Helper on `UTimeWeatherSubsystem`:
```cpp
// Linearly interpolates blend alphas and overlay alphas between two states.
// Season fields are taken from StateA when SpatialAlpha < 0.5, StateB otherwise.
static FWeatherBlendState GetBlendedWeatherState(
    const FWeatherBlendState& StateA,
    const FWeatherBlendState& StateB,
    float SpatialAlpha);
```

**Note:** This helper is intentionally dumb — it lerps numeric fields. The area system is responsible for choosing which two contexts to blend and supplying the alpha. This system never queries spatial data.

---

## IWeatherContextProvider

Implemented by region actors (or any world object) to supply a custom `UWeatherContextAsset`. The subsystem queries this interface; it has zero knowledge of what the implementing object is.

```cpp
UINTERFACE(MinimalAPI, BlueprintType)
class UWeatherContextProvider : public UInterface { GENERATED_BODY() };

class GAMECORE_API IWeatherContextProvider
{
    GENERATED_BODY()
public:
    // Returns the weather context for this region.
    // Must not return null — return the global context as fallback if needed.
    UFUNCTION(BlueprintNativeEvent)
    UWeatherContextAsset* GetWeatherContext() const;

    // Stable ID used as part of the per-day weather seed.
    // Must be unique per context in the world. Use a GUID baked at asset creation.
    UFUNCTION(BlueprintNativeEvent)
    FGuid GetContextId() const;
};
```

**Important:** Region actors that implement this interface should call `UTimeWeatherSubsystem::RegisterContextProvider` in `BeginPlay` and `UnregisterContextProvider` in `EndPlay` so the subsystem tracks them.
