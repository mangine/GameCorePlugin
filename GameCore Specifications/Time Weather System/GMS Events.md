# GMS Events

**Sub-page of:** [Time Weather System](../Time%20Weather%20System.md)

All GMS channels broadcast by the Time Weather System. Declared in `TimeWeatherEventMessages.h`. All channels are `ServerOnly` unless noted.

---

## Channel Tag Definitions (`DefaultGameplayTags.ini`)

```ini
+GameplayTagList=(Tag="GameCoreEvent.Time.DawnBegan",DevComment="Fires once per day when NormDayTime crosses DawnThreshold")
+GameplayTagList=(Tag="GameCoreEvent.Time.DuskBegan",DevComment="Fires once per day when NormDayTime crosses DuskThreshold")
+GameplayTagList=(Tag="GameCoreEvent.Time.DayRolledOver",DevComment="Fires when the in-game day index increments")
+GameplayTagList=(Tag="GameCoreEvent.Time.SeasonChanged",DevComment="Fires when the resolved current season tag changes")
+GameplayTagList=(Tag="GameCoreEvent.Weather.StateChanged",DevComment="Fires when BaseWeatherA, BaseWeatherB, or OverlayWeather tag changes")
+GameplayTagList=(Tag="GameCoreEvent.Weather.EventActivated",DevComment="Fires when an overlay event becomes the active top-priority event")
+GameplayTagList=(Tag="GameCoreEvent.Weather.EventCompleted",DevComment="Fires when an overlay event fully fades out and is removed")
```

---

## Payload Structs

```cpp
// TimeWeatherEventMessages.h

USTRUCT()
struct FTimeEvent_DawnDusk
{
    GENERATED_BODY()
    int32        Day        = 0;     // current in-game day index
    float        NormTime   = 0.f;  // normalized day time when fired
    FGameplayTag ContextId;          // which context (invalid = global)
};

USTRUCT()
struct FTimeEvent_DayRolledOver
{
    GENERATED_BODY()
    int32 NewDay     = 0;
    int32 DayOfYear  = 0;   // NewDay % YearLengthDays
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
    int32        Priority        = 0;
    float        FadeInSeconds   = 0.f;
    float        SustainSeconds  = 0.f;
    float        FadeOutSeconds  = 0.f;
    FGuid        EventId;         // cancellation handle
    FGuid        ContextId;       // FGuid() = global
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

## Broadcast Call Sites

```cpp
// Dawn example — called from BroadcastTimeEvents():
void UTimeWeatherSubsystem::BroadcastTimeEvents(float NormDayTime, int32 CurrentDay)
{
    auto* Bus = UGameCoreEventSubsystem::Get(this);
    if (!Bus) return;

    if (!bDawnFired && NormDayTime >= Config->DawnThreshold)
    {
        bDawnFired = true;
        FTimeEvent_DawnDusk Payload;
        Payload.Day      = CurrentDay;
        Payload.NormTime = NormDayTime;
        Bus->Broadcast(TAG_GameCoreEvent_Time_DawnBegan, Payload,
            EGameCoreEventScope::ServerOnly);
    }

    if (!bDuskFired && NormDayTime >= Config->DuskThreshold)
    {
        bDuskFired = true;
        FTimeEvent_DawnDusk Payload;
        Payload.Day      = CurrentDay;
        Payload.NormTime = NormDayTime;
        Bus->Broadcast(TAG_GameCoreEvent_Time_DuskBegan, Payload,
            EGameCoreEventScope::ServerOnly);
    }
}

// Weather state changed:
void UTimeWeatherSubsystem::BroadcastWeatherChanged(const FContextState& State)
{
    auto* Bus = UGameCoreEventSubsystem::Get(this);
    if (!Bus) return;

    FWeatherEvent_StateChanged Payload;
    Payload.NewState  = State.CurrentBlend;
    Payload.ContextId = State.Provider
        ? State.Provider->GetContextId()
        : FGuid();

    Bus->Broadcast(TAG_GameCoreEvent_Weather_StateChanged, Payload,
        EGameCoreEventScope::ServerOnly);
}
```

---

## Design Rules

- Never broadcast on alpha-only changes (BlendAlpha, OverlayAlpha moving between 0 and 1). Only broadcast when **tags** change.
- Dawn/Dusk fire once per day per context via `bDawnFired`/`bDuskFired` bool flags that reset on day rollover.
- `FWeatherEvent_StateChanged` carries the full `FWeatherBlendState` — subscribers do not need to call back into the subsystem.
