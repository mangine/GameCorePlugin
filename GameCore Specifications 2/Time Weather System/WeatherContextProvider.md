# WeatherContextProvider

**File:** `WeatherContextProvider.h`  
**Part of:** Time Weather System

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

    // Stable GUID used as the per-day deterministic weather seed input
    // and as the key in UTimeWeatherSubsystem::ContextStates.
    // Must be unique per live context.
    // Bake into the actor at edit-time with EditInstanceOnly + editor-assigned value.
    // Do NOT generate at runtime — that breaks seed determinism across restarts.
    UFUNCTION(BlueprintNativeEvent, Category="TimeWeather")
    FGuid GetContextId() const;
};
```

**Lifecycle contract:**

- Call `UTimeWeatherSubsystem::RegisterContextProvider(this)` in `BeginPlay` **server only** (`HasAuthority()`).
- Call `UTimeWeatherSubsystem::UnregisterContextProvider(this)` in `EndPlay` **server only**.
- Failure to unregister results in a dangling raw pointer in `ContextStates` — this is a hard requirement.

**GUID assignment:** The GUID must be stable across server restarts (baked into the level). Providing the same context GUID for two different actors in the same world results in a duplicate registration warning and the second provider being ignored.
