#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "TimeWeatherConfig.generated.h"

class UWeatherContextAsset;

// =============================================================================
// UTimeWeatherProjectSettings
// =============================================================================

/**
 * Project Settings bridge for the Time Weather system.
 * Appears in Project Settings → GameCore → TimeWeather.
 * UTimeWeatherSubsystem calls GetDefault<UTimeWeatherProjectSettings>() in Initialize.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Time Weather"))
class GAMECORE_API UTimeWeatherProjectSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UTimeWeatherProjectSettings()
    {
        CategoryName = TEXT("GameCore");
        SectionName  = TEXT("TimeWeather");
    }

    /**
     * The config data asset driving the entire Time Weather system.
     * Must be set before PIE or server startup.
     */
    UPROPERTY(Config, EditAnywhere, Category="Config",
        meta=(AllowedClasses="TimeWeatherConfig"))
    TSoftObjectPtr<UTimeWeatherConfig> TimeWeatherConfig;

    virtual FName GetContainerName() const override { return TEXT("Project"); }
    virtual FName GetCategoryName()  const override { return TEXT("GameCore"); }
    virtual FName GetSectionName()   const override { return TEXT("TimeWeather"); }
};

// =============================================================================
// UTimeWeatherConfig
// =============================================================================

/**
 * Data asset holding all global time and weather parameters.
 * Referenced by UTimeWeatherProjectSettings::TimeWeatherConfig.
 * Contains no per-frame logic.
 */
UCLASS(BlueprintType)
class GAMECORE_API UTimeWeatherConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    /**
     * Real Unix seconds mapped to in-game day 0.
     * Example: 1735689600 = 2025-01-01 00:00:00 UTC.
     * Set to 0 to start from the Unix epoch.
     */
    UPROPERTY(EditDefaultsOnly, Category="Time")
    int64 ServerEpochOffsetSeconds = 0;

    /**
     * Real seconds per in-game day. Minimum 60.
     * Common values: 1200 (20 min), 2400 (40 min), 4800 (80 min).
     */
    UPROPERTY(EditDefaultsOnly, Category="Time", meta=(ClampMin=60.f))
    float DayDurationSeconds = 1200.f;

    /**
     * Total in-game days that make up one full year (all seasons combined).
     * Example: 4 seasons × 30 days = 120.
     */
    UPROPERTY(EditDefaultsOnly, Category="Time", meta=(ClampMin=1))
    int32 YearLengthDays = 120;

    /**
     * Fixed integer mixed with day index and context GUID to produce
     * a per-day, per-context weather seed.
     * Change this to globally reseed all weather.
     */
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    int32 WeatherSeedBase = 42;

    /**
     * The global weather context.
     * Must not be null — the subsystem will log an error and disable itself if unset.
     */
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    TObjectPtr<UWeatherContextAsset> GlobalContext;

    /**
     * Normalised day time [0,1) at which the DawnBegan GMS event fires.
     * Default 0.25 = quarter-day (6am equivalent).
     */
    UPROPERTY(EditDefaultsOnly, Category="Events",
        meta=(ClampMin=0.f, ClampMax=1.f))
    float DawnThreshold = 0.25f;

    /**
     * Normalised day time [0,1) at which the DuskBegan GMS event fires.
     * Default 0.75 = three-quarter-day (6pm equivalent).
     */
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
