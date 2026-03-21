# TimeWeatherEventMessages

**File:** `TimeWeatherEventMessages.h`  
**Part of:** Time Weather System

All GMS channel tags and payload structs broadcast by the Time Weather System. All channels are `ServerOnly`.

---

## Channel Tag Definitions

Add to `DefaultGameplayTags.ini`:

```ini
+GameplayTagList=(Tag="GameCoreEvent.Time.DawnBegan",DevComment="Fires once per day per context at DawnThreshold")
+GameplayTagList=(Tag="GameCoreEvent.Time.DuskBegan",DevComment="Fires once per day per context at DuskThreshold")
+GameplayTagList=(Tag="GameCoreEvent.Time.DayRolledOver",DevComment="Fires when the in-game day index increments")
+GameplayTagList=(Tag="GameCoreEvent.Time.SeasonChanged",DevComment="Fires when the resolved current season tag changes for a context")
+GameplayTagList=(Tag="GameCoreEvent.Weather.StateChanged",DevComment="Fires when BaseWeatherA, BaseWeatherB, or OverlayWeather tag changes")
+GameplayTagList=(Tag="GameCoreEvent.Weather.EventActivated",DevComment="Fires when an overlay event becomes the active top-priority event")
+GameplayTagList=(Tag="GameCoreEvent.Weather.EventCompleted",DevComment="Fires when an overlay event fully fades out and is removed")
```

---

## Payload Structs

```cpp
USTRUCT()
struct FTimeEvent_DawnDusk
{
    GENERATED_BODY()
    int32 Day       = 0;     // current in-game day index
    float NormTime  = 0.f;   // normalised day time when fired
    FGuid ContextId;          // FGuid() = global context
};

USTRUCT()
struct FTimeEvent_DayRolledOver
{
    GENERATED_BODY()
    int32 NewDay    = 0;
    int32 DayOfYear = 0;     // NewDay % YearLengthDays
};

USTRUCT()
struct FTimeEvent_SeasonChanged
{
    GENERATED_BODY()
    FGameplayTag PreviousSeason;
    FGameplayTag NewSeason;
    FGuid        ContextId;   // FGuid() = global
};

USTRUCT()
struct FWeatherEvent_StateChanged
{
    GENERATED_BODY()
    FWeatherBlendState NewState;
    FGuid              ContextId;   // FGuid() = global
};

USTRUCT()
struct FWeatherEvent_EventActivated
{
    GENERATED_BODY()
    FGameplayTag EventWeatherTag;
    int32        Priority       = 0;
    float        FadeInSeconds  = 0.f;
    float        SustainSeconds = 0.f;
    float        FadeOutSeconds = 0.f;
    FGuid        EventId;        // cancellation handle returned by TriggerOverlayEvent
    FGuid        ContextId;      // FGuid() = global
};

USTRUCT()
struct FWeatherEvent_EventCompleted
{
    GENERATED_BODY()
    FGameplayTag EventWeatherTag;
    FGuid        EventId;
    FGuid        ContextId;
};
```

---

## Channel Summary

| Channel Tag | Payload Struct | Scope | Frequency |
|---|---|---|---|
| `GameCoreEvent.Time.DawnBegan` | `FTimeEvent_DawnDusk` | ServerOnly | Once per day per context |
| `GameCoreEvent.Time.DuskBegan` | `FTimeEvent_DawnDusk` | ServerOnly | Once per day per context |
| `GameCoreEvent.Time.DayRolledOver` | `FTimeEvent_DayRolledOver` | ServerOnly | Once per in-game day (global) |
| `GameCoreEvent.Time.SeasonChanged` | `FTimeEvent_SeasonChanged` | ServerOnly | On season tag change per context |
| `GameCoreEvent.Weather.StateChanged` | `FWeatherEvent_StateChanged` | ServerOnly | On base or overlay tag change per context |
| `GameCoreEvent.Weather.EventActivated` | `FWeatherEvent_EventActivated` | ServerOnly | On overlay event becoming top-priority |
| `GameCoreEvent.Weather.EventCompleted` | `FWeatherEvent_EventCompleted` | ServerOnly | On overlay event fully expired |

---

## Design Rules

- **Never broadcast on alpha-only changes.** Only broadcast when **tags** change (`BaseWeatherA`, `BaseWeatherB`, `OverlayWeather`).
- `FTimeEvent_DawnDusk` carries `FGuid ContextId`, not `FGameplayTag` — the original spec had a type mismatch here. Use `FGuid` to match all other payloads. `FGuid()` = global context.
- Dawn/Dusk fire once per day per context via `bDawnFired`/`bDuskFired` bools inside `FContextState` that reset on day rollover.
- `FWeatherEvent_StateChanged` carries the full `FWeatherBlendState` — subscribers do not need to call back into the subsystem.
