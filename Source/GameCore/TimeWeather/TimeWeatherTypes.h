#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TimeWeatherTypes.generated.h"

class USeasonDefinition;
class UWeatherEventDefinition;

// =============================================================================
// FGameTimeSnapshot
// =============================================================================

/**
 * Replicated once to clients on connect and on day rollover.
 * Stateless — any holder can derive current time without callbacks.
 * AGameState subclass should hold this as a replicated UPROPERTY.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FGameTimeSnapshot
{
    GENERATED_BODY()

    /** Real Unix seconds aligned to in-game day 0. */
    UPROPERTY() int64 ServerEpochOffsetSeconds = 0;

    /** Real seconds per in-game day. E.g. 1200 = 20-minute days. */
    UPROPERTY() float DayDurationSeconds = 1200.f;

    /** In-game days per full year (complete season cycle). */
    UPROPERTY() int32 YearLengthDays = 120;

    /**
     * Returns [0, 1) position within the current day.
     * 0.0 = midnight, 0.25 = dawn-ish, 0.5 = noon, 0.75 = dusk-ish.
     * NowSeconds must come from FPlatformTime::Seconds() (double).
     */
    float GetNormalizedDayTime(double NowSeconds) const
    {
        double Elapsed = NowSeconds - (double)ServerEpochOffsetSeconds;
        if (Elapsed < 0.0) Elapsed = 0.0;
        double DayFrac = FMath::Fmod(Elapsed / (double)DayDurationSeconds, 1.0);
        return (float)DayFrac;
    }

    /** Absolute in-game day index since epoch. Monotonically increasing. */
    int32 GetCurrentDay(double NowSeconds) const
    {
        double Elapsed = NowSeconds - (double)ServerEpochOffsetSeconds;
        if (Elapsed < 0.0) return 0;
        return (int32)FMath::FloorToDouble(Elapsed / (double)DayDurationSeconds);
    }

    /** Day index within the current year [0, YearLengthDays). */
    int32 GetDayOfYear(double NowSeconds) const
    {
        return GetCurrentDay(NowSeconds) % FMath::Max(1, YearLengthDays);
    }
};

// =============================================================================
// FSeasonRange
// =============================================================================

/**
 * Internal implementation detail of UTimeWeatherSubsystem.
 * Computed at context registration; cached in FContextState; never serialised.
 */
struct FSeasonRange
{
    USeasonDefinition* Season   = nullptr;
    int32              StartDay = 0;  // inclusive, day-of-year
    int32              EndDay   = 0;  // inclusive, day-of-year

    bool ContainsDayOfYear(int32 DayOfYear) const
    {
        return DayOfYear >= StartDay && DayOfYear <= EndDay;
    }

    float GetDurationDays() const
    {
        return (float)(EndDay - StartDay + 1);
    }

    /** Returns how many days into this season DayOfYear is [0, duration). */
    float GetDayWithinSeason(int32 DayOfYear) const
    {
        return (float)FMath::Max(0, DayOfYear - StartDay);
    }
};

// =============================================================================
// FSeasonContext
// =============================================================================

/**
 * Built once per day by ResolveSeasonContext; passed into UWeatherSequence::GetNextWeather.
 * Provides all situational data for deterministic weather selection.
 */
USTRUCT()
struct FSeasonContext
{
    GENERATED_BODY()

    float        NormalizedDayTime  = 0.f;  // [0,1) at schedule build time
    FGameplayTag CurrentSeason;              // invalid tag = no seasons defined
    FGameplayTag PreviousSeason;             // invalid tag = none or first season
    float        SeasonBlendAlpha   = 1.f;  // 1.0 = fully in current season
    int32        CurrentDay         = 0;    // absolute day index
    int32        DayOfYear          = 0;    // [0, YearLengthDays)
    FGameplayTag LastWeather;               // last resolved base weather (cross-day continuity)
};

// =============================================================================
// FWeatherBlendState
// =============================================================================

/**
 * The sole output of the Time Weather System.
 * Downstream consumers (audio, VFX, materials) read only this struct.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FWeatherBlendState
{
    GENERATED_BODY()

    /** Base weather layer — lerp parameters from A toward B using BlendAlpha. */
    UPROPERTY(BlueprintReadOnly) FGameplayTag BaseWeatherA;
    UPROPERTY(BlueprintReadOnly) FGameplayTag BaseWeatherB;
    UPROPERTY(BlueprintReadOnly) float        BlendAlpha       = 0.f;

    /** Overlay event layer. Invalid tag = no overlay (OverlayAlpha = 0). */
    UPROPERTY(BlueprintReadOnly) FGameplayTag OverlayWeather;
    UPROPERTY(BlueprintReadOnly) float        OverlayAlpha     = 0.f;

    /** Season context replicated into state for downstream consumers. */
    UPROPERTY(BlueprintReadOnly) FGameplayTag CurrentSeason;
    UPROPERTY(BlueprintReadOnly) FGameplayTag PreviousSeason;
    UPROPERTY(BlueprintReadOnly) float        SeasonBlendAlpha = 1.f;

    bool IsValid() const { return BaseWeatherA.IsValid(); }
};

// =============================================================================
// FDailyWeatherKeyframe
// =============================================================================

/** One slot in the daily weather schedule. Built once at dawn; never modified during the day. */
USTRUCT()
struct FDailyWeatherKeyframe
{
    GENERATED_BODY()

    /** Normalised day-time [0,1) when this keyframe begins. */
    float StartNormTime      = 0.f;
    /** Normalised day-time [0,1) when the next keyframe begins (exclusive end). */
    float EndNormTime        = 1.f;
    /** Target base weather tag. */
    FGameplayTag WeatherTag;
    /**
     * Real-time seconds for the blend transition at the start of this slot.
     * If 0, the transition is instant.
     */
    float TransitionDuration = 0.f;
};

// =============================================================================
// FDailyWeatherSchedule
// =============================================================================

/** Owned per FContextState. Rebuilt once per day per context at rollover. */
USTRUCT()
struct FDailyWeatherSchedule
{
    GENERATED_BODY()

    /** Ordered by StartNormTime ascending. Built by RebuildDailySchedule. */
    TArray<FDailyWeatherKeyframe> Keyframes;

    /**
     * Returns the keyframe active at NormTime, or nullptr if Keyframes is empty.
     * Array is guaranteed sorted; scans forward — cheap for 3–6 keyframes.
     */
    const FDailyWeatherKeyframe* GetActiveKeyframe(float NormTime) const
    {
        const FDailyWeatherKeyframe* Active = nullptr;
        for (const FDailyWeatherKeyframe& KF : Keyframes)
        {
            if (KF.StartNormTime <= NormTime)
                Active = &KF;
            else
                break;
        }
        return Active;
    }

    void Reset() { Keyframes.Reset(); }
};

// =============================================================================
// FActiveWeatherEvent
// =============================================================================

/** One in-flight overlay event. Lives inside FContextState::ActiveEvents or QueuedEvents. */
USTRUCT()
struct FActiveWeatherEvent
{
    GENERATED_BODY()

    /** Unique handle returned to the caller of TriggerOverlayEvent. Used for cancellation. */
    FGuid EventId;

    /** Pointer to the definition asset. Not owned — asset must remain loaded. */
    UWeatherEventDefinition* Definition = nullptr;

    /** Copied from Definition->Priority at activation time. */
    int32 Priority = 0;

    /** Handle issued by UGameCoreEventBus::RegisterActiveEvent. Used for unregistration. */
    FGuid RegistryHandle;

    /** All times are FPlatformTime::Seconds() (double precision wall-clock). */
    double StartTime    = 0.0;
    double FadeInEnd    = 0.0;   // StartTime + FadeInSeconds
    double SustainEnd   = 0.0;   // FadeInEnd + SustainSeconds
    double FadeOutEnd   = 0.0;   // SustainEnd + FadeOutSeconds

    /** Alpha in [0,1]. Updated once per tick by TickAlpha. */
    float CurrentAlpha = 0.f;

    bool IsExpired(double NowSeconds) const { return NowSeconds >= FadeOutEnd; }

    /** Recomputes CurrentAlpha from wall-clock time. Call once per tick per event. */
    void TickAlpha(double NowSeconds)
    {
        if (NowSeconds <= StartTime)
        {
            CurrentAlpha = 0.f;
        }
        else if (NowSeconds < FadeInEnd)
        {
            CurrentAlpha = (float)FMath::GetMappedRangeValueClamped(
                FVector2D(StartTime, FadeInEnd), FVector2D(0.0, 1.0), NowSeconds);
        }
        else if (NowSeconds < SustainEnd)
        {
            CurrentAlpha = 1.f;
        }
        else
        {
            CurrentAlpha = (float)FMath::GetMappedRangeValueClamped(
                FVector2D(SustainEnd, FadeOutEnd), FVector2D(1.0, 0.0), NowSeconds);
        }
    }
};

// =============================================================================
// FRegionWeatherBlend
// =============================================================================

/**
 * Convenience wrapper for the region-boundary blend helper.
 * The area system computes SpatialAlpha and calls UTimeWeatherSubsystem::GetBlendedWeatherState.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FRegionWeatherBlend
{
    GENERATED_BODY()

    FWeatherBlendState StateA;            // weather state of the first region
    FWeatherBlendState StateB;            // weather state of the second region
    float              SpatialAlpha = 0.f; // 0 = pure A, 1 = pure B
};

// =============================================================================
// FScheduledEventTrigger
// =============================================================================

/**
 * Internal struct. Produced by RollTimedEvents once per day.
 * Checked each tick by TickScheduledTriggers.
 * Array sorted ascending by TriggerNormTime after construction.
 */
struct FScheduledEventTrigger
{
    UWeatherEventDefinition* Event           = nullptr;
    float                    TriggerNormTime = 0.f;
    bool                     bFired          = false;
};
