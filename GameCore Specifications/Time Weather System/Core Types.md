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
    // Set via UTimeWeatherConfig::ServerEpochOffsetSeconds.
    UPROPERTY() int64 ServerEpochOffsetSeconds = 0;

    // Real seconds per in-game day. E.g. 1200 = 20-minute days.
    UPROPERTY() float DayDurationSeconds = 1200.f;

    // In-game days per full year (season cycle).
    UPROPERTY() int32 YearLengthDays = 120;

    // --- Derived helpers (not replicated — computed locally from wall-clock) ---

    // Returns [0, 1) position within the current day.
    // 0.0 = midnight, 0.25 = dawn-ish, 0.5 = noon, 0.75 = dusk-ish.
    // NowSeconds should come from FPlatformTime::Seconds() (returns double).
    float GetNormalizedDayTime(double NowSeconds) const
    {
        double Elapsed = NowSeconds - (double)ServerEpochOffsetSeconds;
        if (Elapsed < 0.0) Elapsed = 0.0;
        double DayFrac = FMath::Fmod(Elapsed / (double)DayDurationSeconds, 1.0);
        return (float)DayFrac;
    }

    // Absolute in-game day index since epoch. Monotonically increasing.
    int32 GetCurrentDay(double NowSeconds) const
    {
        double Elapsed = NowSeconds - (double)ServerEpochOffsetSeconds;
        if (Elapsed < 0.0) return 0;
        return (int32)FMath::FloorToDouble(Elapsed / (double)DayDurationSeconds);
    }

    // Day index within the current year [0, YearLengthDays).
    // Used to determine active season.
    int32 GetDayOfYear(double NowSeconds) const
    {
        return GetCurrentDay(NowSeconds) % FMath::Max(1, YearLengthDays);
    }
};
```

**Note on time source:** All helpers take `double NowSeconds` from `FPlatformTime::Seconds()`, which returns a `double`. Do not cast to `int64` before passing — precision loss causes visible day-boundary glitches at high day counts.

**Client usage:** `AGameState` holds this as a replicated `UPROPERTY`. Clients call `GetNormalizedDayTime(FPlatformTime::Seconds())` locally every frame for display — no server callbacks.

---

## FSeasonRange

Computed at context initialisation from `UWeatherContextAsset`. Cached by the subsystem; never serialised.

```cpp
struct FSeasonRange
{
    USeasonDefinition* Season   = nullptr;
    int32              StartDay = 0;    // inclusive, day-of-year
    int32              EndDay   = 0;    // inclusive, day-of-year

    bool ContainsDayOfYear(int32 DayOfYear) const
    {
        return DayOfYear >= StartDay && DayOfYear <= EndDay;
    }

    float GetDurationDays() const
    {
        return (float)(EndDay - StartDay + 1);
    }

    // Returns how many days into this season DayOfYear is [0, duration).
    float GetDayWithinSeason(int32 DayOfYear) const
    {
        return (float)FMath::Max(0, DayOfYear - StartDay);
    }
};
```

This struct is an internal implementation detail of the subsystem and does not appear in public headers.

---

## FSeasonContext

Built each day by `ResolveSeasonContext` and passed into `UWeatherSequence::GetNextWeather`. Provides sequence authors with all situational data needed for deterministic selection.

```cpp
USTRUCT()
struct FSeasonContext
{
    GENERATED_BODY()

    float        NormalizedDayTime  = 0.f;  // [0,1) at the time of schedule build (dawn)
    FGameplayTag CurrentSeason;              // invalid tag = no season defined
    FGameplayTag PreviousSeason;             // invalid tag = none or first season
    float        SeasonBlendAlpha   = 1.f;  // 1.0 = fully in current season; <1.0 = blending in
    int32        CurrentDay         = 0;    // absolute day index
    int32        DayOfYear          = 0;    // [0, YearLengthDays)
    FGameplayTag LastWeather;               // tag of the last resolved base weather (cross-day continuity)
};
```

---

## FWeatherBlendState

The **sole output** of the Time Weather System. Downstream consumers (VFX, audio, spawners, wave simulation) read only this struct. They never touch sequences, seasons, or event definitions directly.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FWeatherBlendState
{
    GENERATED_BODY()

    // Base weather layer.
    // Downstream systems lerp their parameters from A toward B using BlendAlpha.
    // When a transition completes: A is set to B, BlendAlpha resets to 0, new B is set.
    // On first valid state: both A and B are the same tag, BlendAlpha = 1.
    UPROPERTY(BlueprintReadOnly) FGameplayTag BaseWeatherA;
    UPROPERTY(BlueprintReadOnly) FGameplayTag BaseWeatherB;
    UPROPERTY(BlueprintReadOnly) float        BlendAlpha    = 0.f;

    // Overlay event layer. Applied on top of the base blend.
    // OverlayAlpha drives how strongly the overlay replaces/augments the base.
    // Invalid tag = no overlay active (OverlayAlpha will be 0).
    UPROPERTY(BlueprintReadOnly) FGameplayTag OverlayWeather;
    UPROPERTY(BlueprintReadOnly) float        OverlayAlpha  = 0.f;

    // Season context replicated into the state for downstream consumers
    // that need seasonal data (foliage density, NPC schedules, spawn tables).
    UPROPERTY(BlueprintReadOnly) FGameplayTag CurrentSeason;
    UPROPERTY(BlueprintReadOnly) FGameplayTag PreviousSeason;
    UPROPERTY(BlueprintReadOnly) float        SeasonBlendAlpha = 1.f;

    bool IsValid() const { return BaseWeatherA.IsValid(); }
};
```

---

## FDailyWeatherKeyframe

One slot in the daily weather schedule. Built once at dawn; never modified during the day.

```cpp
USTRUCT()
struct FDailyWeatherKeyframe
{
    GENERATED_BODY()

    // Normalized day-time [0,1) when this keyframe begins.
    float StartNormTime    = 0.f;
    // Normalized day-time [0,1) when the next keyframe begins (exclusive end).
    float EndNormTime      = 1.f;
    // The target base weather tag for this slot.
    FGameplayTag WeatherTag;
    // Real-time seconds for the blend transition (BlendAlpha 0→1) at the start of this slot.
    // If 0, the transition is instant.
    float TransitionDuration = 0.f;
};
```

---

## FDailyWeatherSchedule

Owned by each `FContextState`. Rebuilt once per day per context at rollover.

```cpp
USTRUCT()
struct FDailyWeatherSchedule
{
    GENERATED_BODY()

    // Ordered by StartNormTime ascending. Built by RebuildDailySchedule.
    TArray<FDailyWeatherKeyframe> Keyframes;

    // Returns the keyframe active at NormTime, or nullptr if Keyframes is empty.
    // Scans forward — cheap for small arrays (3–6 keyframes).
    const FDailyWeatherKeyframe* GetActiveKeyframe(float NormTime) const
    {
        const FDailyWeatherKeyframe* Active = nullptr;
        for (const FDailyWeatherKeyframe& KF : Keyframes)
        {
            if (KF.StartNormTime <= NormTime)
                Active = &KF;
            else
                break; // sorted, so no later entry can match
        }
        return Active;
    }

    void Reset()
    {
        Keyframes.Reset();
    }
};
```

**Note:** `GetActiveKeyframe` returns the last keyframe whose `StartNormTime <= NormTime`. The final keyframe in the array covers through to `1.0` (end of day). The array is guaranteed sorted because `RebuildDailySchedule` builds it in order.

---

## FActiveWeatherEvent

Represents one in-flight overlay event. Lives inside `FContextState::ActiveEvents` or `FContextState::QueuedEvents`.

```cpp
USTRUCT()
struct FActiveWeatherEvent
{
    GENERATED_BODY()

    // Unique handle returned to the caller of TriggerOverlayEvent. Used for cancellation.
    FGuid EventId;

    // Pointer to the definition asset. Not owned — asset must remain loaded.
    UWeatherEventDefinition* Definition = nullptr;

    // Copied from Definition->Priority at activation time.
    int32 Priority = 0;

    // Handle issued by UGameCoreEventSubsystem::RegisterActiveEvent.
    // Used to unregister from the shared registry on expiry or cancellation.
    FGuid RegistryHandle;

    // All times are FPlatformTime::Seconds() (double precision wall-clock).
    double StartTime    = 0.0;
    double FadeInEnd    = 0.0;   // StartTime + FadeInSeconds
    double SustainEnd   = 0.0;   // FadeInEnd + SustainSeconds
    double FadeOutEnd   = 0.0;   // SustainEnd + FadeOutSeconds

    // Alpha in [0,1]. Updated once per tick by TickAlpha.
    float CurrentAlpha = 0.f;

    bool IsExpired(double NowSeconds) const { return NowSeconds >= FadeOutEnd; }

    // Recomputes CurrentAlpha from wall-clock time. Call once per Tick per event.
    void TickAlpha(double NowSeconds)
    {
        if (NowSeconds <= StartTime)
        {
            CurrentAlpha = 0.f;
        }
        else if (NowSeconds < FadeInEnd)
        {
            // Fade-in phase: 0 → 1
            CurrentAlpha = (float)FMath::GetMappedRangeValueClamped(
                FVector2D(StartTime, FadeInEnd), FVector2D(0.0, 1.0), NowSeconds);
        }
        else if (NowSeconds < SustainEnd)
        {
            // Sustain phase: hold at 1
            CurrentAlpha = 1.f;
        }
        else
        {
            // Fade-out phase: 1 → 0
            CurrentAlpha = (float)FMath::GetMappedRangeValueClamped(
                FVector2D(SustainEnd, FadeOutEnd), FVector2D(1.0, 0.0), NowSeconds);
        }
    }
};
```

**Important:** When `CancelOverlayEvent` is called, it does **not** remove the event immediately. It clamps `SustainEnd` to `Now` and sets `FadeOutEnd = Now + ProportionalFadeOut`, causing the event to begin fading out on the next `TickAlpha`. The event is only removed when `IsExpired` returns `true`.

---

## FRegionWeatherBlend

Future integration point. The area system computes `SpatialAlpha` and calls `UTimeWeatherSubsystem::GetBlendedWeatherState`. This struct is a convenience wrapper for that call.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRegionWeatherBlend
{
    GENERATED_BODY()

    FWeatherBlendState StateA;       // weather state of the first region
    FWeatherBlendState StateB;       // weather state of the second region
    float              SpatialAlpha; // 0 = pure A, 1 = pure B — driven by area system
};
```

`GetBlendedWeatherState` implementation:

```cpp
// static
FWeatherBlendState UTimeWeatherSubsystem::GetBlendedWeatherState(
    const FWeatherBlendState& A, const FWeatherBlendState& B, float Alpha)
{
    // Simple lerp of float fields. Tag fields are winner-takes-all at 0.5 threshold.
    // The area system is expected to supply a smooth alpha so hard flips are rare.
    FWeatherBlendState Result;
    const FWeatherBlendState& Dominant = (Alpha < 0.5f) ? A : B;

    Result.BaseWeatherA     = Dominant.BaseWeatherA;
    Result.BaseWeatherB     = Dominant.BaseWeatherB;
    Result.BlendAlpha       = FMath::Lerp(A.BlendAlpha, B.BlendAlpha, Alpha);
    Result.OverlayWeather   = Dominant.OverlayWeather;
    Result.OverlayAlpha     = FMath::Lerp(A.OverlayAlpha, B.OverlayAlpha, Alpha);
    Result.CurrentSeason    = Dominant.CurrentSeason;
    Result.PreviousSeason   = Dominant.PreviousSeason;
    Result.SeasonBlendAlpha = FMath::Lerp(A.SeasonBlendAlpha, B.SeasonBlendAlpha, Alpha);
    return Result;
}
```

---

## IWeatherContextProvider

Implemented by region actors to supply an independent `UWeatherContextAsset`. The subsystem is completely unaware of what the implementing actor is.

```cpp
UINTERFACE(MinimalAPI, BlueprintType)
class UWeatherContextProvider : public UInterface { GENERATED_BODY() };

class GAMECORE_API IWeatherContextProvider
{
    GENERATED_BODY()
public:
    // Returns the weather context for this region.
    // Must never return nullptr. Return the global context as a fallback if needed.
    UFUNCTION(BlueprintNativeEvent, Category="TimeWeather")
    UWeatherContextAsset* GetWeatherContext() const;

    // Stable GUID used as part of the per-day deterministic weather seed
    // and as the key in UTimeWeatherSubsystem::ContextStates.
    // Must be unique per live context. Bake this GUID into the actor at edit-time
    // using a UPROPERTY(EditInstanceOnly) + editor-assigned value.
    // Do NOT generate it at runtime — that breaks seed determinism across restarts.
    UFUNCTION(BlueprintNativeEvent, Category="TimeWeather")
    FGuid GetContextId() const;
};
```

**Lifecycle contract:** Region actors must call `UTimeWeatherSubsystem::RegisterContextProvider(this)` in `BeginPlay` (server only) and `UnregisterContextProvider(this)` in `EndPlay`. Failure to unregister results in a dangling pointer in `ContextStates`.
