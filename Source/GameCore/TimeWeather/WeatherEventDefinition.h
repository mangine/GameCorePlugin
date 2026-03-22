#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "WeatherEventDefinition.generated.h"

/**
 * Defines one overlay weather event: its weather tag, lifecycle durations, and priority.
 * All duration fields are real-time seconds (not in-game time).
 */
UCLASS(BlueprintType)
class GAMECORE_API UWeatherEventDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    /** Gameplay Tag applied as the overlay in FWeatherBlendState::OverlayWeather. */
    UPROPERTY(EditDefaultsOnly, Category="Identity")
    FGameplayTag WeatherTag;

    /** Higher value wins when two events compete for the overlay slot on the same context. */
    UPROPERTY(EditDefaultsOnly, Category="Priority")
    int32 Priority = 0;

    /** Real-time seconds to fade overlay in (OverlayAlpha 0→1). */
    UPROPERTY(EditDefaultsOnly, Category="Lifecycle", meta=(ClampMin=0.f))
    float FadeInSeconds = 60.f;

    /** Real-time seconds the overlay holds at full alpha after fade-in. */
    UPROPERTY(EditDefaultsOnly, Category="Lifecycle", meta=(ClampMin=0.f))
    float SustainSeconds = 300.f;

    /** Real-time seconds to fade overlay out (OverlayAlpha 1→0). */
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
