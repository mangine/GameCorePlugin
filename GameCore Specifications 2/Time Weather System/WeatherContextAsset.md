# WeatherContextAsset

**Files:** `WeatherContextAsset.h/.cpp`  
**Part of:** Time Weather System

---

## UWeatherContextAsset

Full weather configuration for one context (global world or a region). Referenced by `UTimeWeatherConfig::GlobalContext` and by region actors via `IWeatherContextProvider::GetWeatherContext()`.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UWeatherContextAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    // Ordered season list evaluated cyclically against day-of-year.
    // Empty = no season cycling. DefaultWeatherSequence and DefaultDayNightCurve
    // are used at all times.
    UPROPERTY(EditDefaultsOnly, Category="Seasons")
    TArray<TObjectPtr<USeasonDefinition>> Seasons;

    // Fixed day count per season. All seasons receive the same allocation.
    // 0 = divide YearLengthDays equally; remainder days added to the last season.
    UPROPERTY(EditDefaultsOnly, Category="Seasons", meta=(ClampMin=0))
    int32 DaysPerSeason = 0;

    // Used when Seasons is empty, or as fallback if the active season
    // has no WeatherSequence set.
    UPROPERTY(EditDefaultsOnly, Category="Weather")
    TObjectPtr<UWeatherSequence> DefaultWeatherSequence;

    // Day-night intensity curve. X = normalised day time [0,1), Y = daylight [0,1].
    // Flat at Y=0.0 = permanent night. Flat at Y=1.0 = permanent day.
    // Used when Seasons is empty, or as fallback if the active season has no DayNightCurve.
    UPROPERTY(EditDefaultsOnly, Category="DayNight")
    TObjectPtr<UCurveFloat> DefaultDayNightCurve;

    // Computes ordered season ranges for a given year length.
    // Called once at context registration and cached. Not called per-tick.
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
