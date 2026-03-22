#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WeatherContextProvider.generated.h"

class UWeatherContextAsset;

UINTERFACE(MinimalAPI, BlueprintType)
class UWeatherContextProvider : public UInterface { GENERATED_BODY() };

/**
 * Implemented by region actors to supply an independent UWeatherContextAsset.
 * The subsystem is completely unaware of what the implementing actor is.
 *
 * Lifecycle contract:
 *   - Call UTimeWeatherSubsystem::RegisterContextProvider(this) in BeginPlay (server only).
 *   - Call UTimeWeatherSubsystem::UnregisterContextProvider(this) in EndPlay (server only).
 *   Failure to unregister results in a dangling raw pointer in ContextStates.
 *
 * GUID assignment: Must be stable across server restarts (baked into the level).
 * Two actors with the same GUID in the same world: second registration is ignored.
 */
class GAMECORE_API IWeatherContextProvider
{
    GENERATED_BODY()
public:
    /**
     * Returns the weather context for this region.
     * Must never return nullptr. Return the global context as a fallback if needed.
     */
    UFUNCTION(BlueprintNativeEvent, Category="TimeWeather")
    UWeatherContextAsset* GetWeatherContext() const;

    /**
     * Stable GUID used as the per-day deterministic weather seed input
     * and as the key in UTimeWeatherSubsystem::ContextStates.
     * Must be unique per live context.
     * Bake into the actor at edit-time — do NOT generate at runtime.
     */
    UFUNCTION(BlueprintNativeEvent, Category="TimeWeather")
    FGuid GetContextId() const;
};
