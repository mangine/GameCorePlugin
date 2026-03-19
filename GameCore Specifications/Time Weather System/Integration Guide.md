# Integration Guide

**Sub-page of:** [Time Weather System](../Time%20Weather%20System.md)

End-to-end setup, wiring, and usage samples.

---

## Module Dependencies

```csharp
// In your game module's .Build.cs:
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "GameplayTags",
    "Engine",
});
```

No additional modules are required. The Time Weather System lives entirely within `GameCore`.

---

## Step 1 — Project Settings

Create a `UTimeWeatherConfig` data asset and register it:

```ini
; DefaultGame.ini
[/Script/GameCore.TimeWeatherProjectSettings]
TimeWeatherConfig=/Game/Data/TimeWeather/DA_TimeWeatherConfig.DA_TimeWeatherConfig
```

This is the only INI change required. The subsystem reads this at `Initialize` via `GetDefault<UTimeWeatherProjectSettings>()`.

---

## Step 2 — UTimeWeatherConfig Asset

Right-click in the Content Browser → **GameCore → Time Weather Config**. Typical values:

| Property | Example Value | Notes |
|---|---|---|
| `ServerEpochOffsetSeconds` | `1735689600` | 2025-01-01 UTC → in-game day 0 = game launch |
| `DayDurationSeconds` | `1200` | 20-minute days |
| `YearLengthDays` | `120` | 4 seasons × 30 days |
| `WeatherSeedBase` | `7331` | Any integer; change to reseed all weather globally |
| `GlobalContext` | `DA_Context_World` | Must be set |
| `DawnThreshold` | `0.25` | Quarter-day |
| `DuskThreshold` | `0.75` | Three-quarter-day |

`IsDataValid` will error in the editor if `GlobalContext` is null or thresholds are invalid.

---

## Step 3 — AGameState Subclass (Required)

The subsystem replicates `FGameTimeSnapshot` to clients via `AGameState`. The game module must provide the subclass:

```cpp
// MyGameState.h
UCLASS()
class AMyGameState : public AGameState
{
    GENERATED_BODY()
public:
    // Replicated to all clients. Updated once per day by UTimeWeatherSubsystem.
    UPROPERTY(ReplicatedUsing=OnRep_TimeSnapshot)
    FGameTimeSnapshot TimeSnapshot;

    UFUNCTION()
    void OnRep_TimeSnapshot()
    {
        // Notify client systems that the snapshot has updated.
        // Clients can now call TimeSnapshot.GetNormalizedDayTime(FPlatformTime::Seconds())
        // for accurate local time without further server calls.
        OnTimeSnapshotUpdated.Broadcast(TimeSnapshot);
    }

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
        FOnTimeSnapshotUpdated, const FGameTimeSnapshot&, Snapshot);
    UPROPERTY(BlueprintAssignable)
    FOnTimeSnapshotUpdated OnTimeSnapshotUpdated;

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override
    {
        Super::GetLifetimeReplicatedProps(OutLifetimeProps);
        DOREPLIFETIME(AMyGameState, TimeSnapshot);
    }
};
```

`UTimeWeatherSubsystem::PushSnapshotToClients` casts to `AMyGameState` and sets `TimeSnapshot`. Make sure `DefaultEngine.ini` references your subclass:

```ini
[/Script/Engine.GameMode]
GameStateClass=/Script/MyGame.MyGameState
```

---

## Step 4 — Global Context Asset

Create a `UWeatherContextAsset` at e.g. `/Game/Data/TimeWeather/DA_Context_World`:

- Add four `USeasonDefinition` assets to `Seasons` in order: Spring, Summer, Autumn, Winter.
- Assign a `UCurveFloat` to `DefaultDayNightCurve` (X=day time [0,1), Y=daylight [0,1]).
- Leave `DaysPerSeason = 0` to divide `YearLengthDays` equally.

**Minimal single-weather world (no seasons):**
- Leave `Seasons` empty.
- Assign a `UWeatherSequence_Random` with one entry to `DefaultWeatherSequence`.
- Assign a flat curve at Y=1 to `DefaultDayNightCurve`.

---

## Step 5 — Season Assets

```
DA_Season_Summer:
  SeasonTag            = Season.Summer
  TransitionInPercent  = 15          // first 15% of Summer blends in from Spring
  DayNightCurve        = Curve_LongDay
  WeatherSequence      = DA_Seq_Summer
  TimedEvents:
    [0] Event=DA_Event_HeavyRain  WindowStart=0.5  WindowEnd=0.75  DailyProbability=0.35
    [1] Event=DA_Event_Thunderstorm WindowStart=0.6 WindowEnd=0.8  DailyProbability=0.1
```

---

## Step 6 — Weather Sequences

**Random with neighbour filter (recommended for most regions):**

```
DA_Seq_Summer (UWeatherSequence_Random):
  KeyframesPerDay = 3
  Entries:
    { WeatherTag=Weather.Clear,       Weight=3, ValidPredecessors=[] }
    { WeatherTag=Weather.PartlyCloudy, Weight=2, ValidPredecessors=[] }
    { WeatherTag=Weather.Overcast,     Weight=1,
      ValidPredecessors=[Weather.PartlyCloudy, Weather.Rain],
      TransitionMin=120, TransitionMax=240 }
    { WeatherTag=Weather.Rain,         Weight=1,
      ValidPredecessors=[Weather.Overcast],
      TransitionMin=180, TransitionMax=360 }
```

With this config: Rain can only follow Overcast; Overcast can only follow PartlyCloudy or Rain. Clear skies never jump directly to rain.

---

## Step 7 — Permanent Night / Seasonless Region

```
DA_Context_Permafrost:
  Seasons              = []                        // no season cycling
  DefaultDayNightCurve = DA_Curve_PermanentNight   // UCurveFloat flat at Y=0
  DefaultWeatherSequence = DA_Seq_Blizzard
```

Region actor implementing `IWeatherContextProvider`:

```cpp
UCLASS()
class APermafrostRegion : public AActor, public IWeatherContextProvider
{
    GENERATED_BODY()

    // Set once in the editor per placed instance. Never auto-generate at runtime.
    UPROPERTY(EditInstanceOnly, Category="TimeWeather")
    FGuid ContextIdGuid;

    UPROPERTY(EditInstanceOnly, Category="TimeWeather")
    TObjectPtr<UWeatherContextAsset> ContextAsset;

    virtual UWeatherContextAsset* GetWeatherContext_Implementation() const override
    { return ContextAsset; }

    virtual FGuid GetContextId_Implementation() const override
    { return ContextIdGuid; }

    virtual void BeginPlay() override
    {
        Super::BeginPlay();
        if (HasAuthority())
            if (auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>())
                Sub->RegisterContextProvider(this);
    }

    virtual void EndPlay(const EEndPlayReason::Type Reason) override
    {
        if (HasAuthority())
            if (auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>())
                Sub->UnregisterContextProvider(this);
        Super::EndPlay(Reason);
    }
};
```

**Editor workflow:** After placing the actor, click **Generate GUID** (add an `CallInEditor` helper or use the UE GUID generator) and save the level. The GUID must be stable across sessions.

---

## Querying Weather State (Server)

```cpp
// Global context:
auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>();
FWeatherBlendState State = Sub->GetCurrentWeatherState();
// State.BaseWeatherA, BaseWeatherB, BlendAlpha — lerp your VFX params
// State.OverlayWeather, OverlayAlpha — apply storm/fog overlay
// State.CurrentSeason — adjust foliage, spawn tables, etc.

// Specific region:
FWeatherBlendState RegionState = Sub->GetCurrentWeatherState(MyRegionActor);

// Daylight intensity:
float Light = Sub->GetDaylightIntensity(); // [0,1]
```

---

## Subscribing to Weather Events (Server)

```cpp
// Sea system: adjust wave simulation on storm events.
void USeaSimulationComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority()) return;

    if (auto* Bus = UGameCoreEventSubsystem::Get(this))
    {
        // Use GMS listener directly on the underlying UGameplayMessageSubsystem.
        auto* GMS = GetWorld()->GetSubsystem<UGameplayMessageSubsystem>();
        GMS->RegisterListener<FWeatherEvent_EventActivated>(
            TAG_GameCoreEvent_Weather_EventActivated,
            this, &USeaSimulationComponent::OnWeatherEventActivated);
    }
}

void USeaSimulationComponent::OnWeatherEventActivated(
    FGameplayTag, const FWeatherEvent_EventActivated& Msg)
{
    if (Msg.EventWeatherTag.MatchesTag(TAG_Weather_Storm))
        SetTargetWaveIntensity(StormWaveIntensity);
        // Actual blend happens in your wave tick using Msg.FadeInSeconds.
}
```

---

## Triggering / Cancelling an External Event

```cpp
// Boss enrages — trigger a storm on the global context.
void ABossActor::OnEnrage()
{
    auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>();
    StormHandle = Sub->TriggerOverlayEvent(StormEventDef); // global
}

// Boss dies — begin proportional fade-out.
void ABossActor::OnDeath()
{
    auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>();
    Sub->CancelOverlayEvent(StormHandle);
}
```

---

## Cross-System Event Query

```cpp
// Is any storm currently active on any context?
auto* Bus = UGameCoreEventSubsystem::Get(this);
bool bStormActive = Bus->IsEventActive(TAG_Weather_Storm);
// Matches Weather.Storm, Weather.Storm.Heavy, Weather.Storm.Light, etc. (tag hierarchy).

// Get all active storm event GUIDs:
TArray<FGuid> Storms = Bus->GetActiveEvents(TAG_Weather_Storm);
```

---

## Getting Normalized Day Time on a Client

```cpp
// On any client-side actor or component:
if (AMyGameState* GS = GetWorld()->GetGameState<AMyGameState>())
{
    const FGameTimeSnapshot& Snap = GS->TimeSnapshot;
    float NormTime = Snap.GetNormalizedDayTime(FPlatformTime::Seconds());
    float Daylight = MyDayNightCurve->GetFloatValue(NormTime);
    // Use Daylight to drive sky material parameter, post-process exposure, etc.
}
```

---

## Region Boundary Blending (Future / Area System Integration)

```cpp
// The area system computes SpatialAlpha [0,1] for the player's position
// between two regions, then calls:
FWeatherBlendState Merged = UTimeWeatherSubsystem::GetBlendedWeatherState(
    Sub->GetCurrentWeatherState(RegionA),
    Sub->GetCurrentWeatherState(RegionB),
    SpatialAlpha);
// Feed Merged into your VFX/audio systems.
```

---

## Gameplay Tags Required

Add to `DefaultGameplayTags.ini`:

```ini
; ---- Time events ----
+GameplayTagList=(Tag="GameCoreEvent.Time.DawnBegan",DevComment="Fires once per day per context at DawnThreshold")
+GameplayTagList=(Tag="GameCoreEvent.Time.DuskBegan",DevComment="Fires once per day per context at DuskThreshold")
+GameplayTagList=(Tag="GameCoreEvent.Time.DayRolledOver",DevComment="Fires when in-game day index increments")
+GameplayTagList=(Tag="GameCoreEvent.Time.SeasonChanged",DevComment="Fires when current season tag changes for a context")

; ---- Weather events ----
+GameplayTagList=(Tag="GameCoreEvent.Weather.StateChanged",DevComment="Fires when base or overlay weather tag changes")
+GameplayTagList=(Tag="GameCoreEvent.Weather.EventActivated",DevComment="Fires when an overlay event becomes top priority")
+GameplayTagList=(Tag="GameCoreEvent.Weather.EventCompleted",DevComment="Fires when an overlay event finishes and is removed")

; ---- Season tags (game-defined, opaque to this system) ----
+GameplayTagList=(Tag="Season.Spring")
+GameplayTagList=(Tag="Season.Summer")
+GameplayTagList=(Tag="Season.Autumn")
+GameplayTagList=(Tag="Season.Winter")
+GameplayTagList=(Tag="Season.Permafrost")

; ---- Weather state tags (game-defined, opaque to this system) ----
+GameplayTagList=(Tag="Weather.Clear")
+GameplayTagList=(Tag="Weather.PartlyCloudy")
+GameplayTagList=(Tag="Weather.Overcast")
+GameplayTagList=(Tag="Weather.Rain")
+GameplayTagList=(Tag="Weather.Storm")
+GameplayTagList=(Tag="Weather.Storm.Heavy")
+GameplayTagList=(Tag="Weather.Storm.Light")
+GameplayTagList=(Tag="Weather.Fog")
+GameplayTagList=(Tag="Weather.Blizzard")
```

Weather and season tags are **completely opaque** to the Time Weather System. It passes them through as `FGameplayTag` values. Your VFX, audio, and gameplay systems decide what each tag means.

---

## What This System Does NOT Do

- Does not render anything. No sky, no particles, no post-process.
- Does not replicate `FWeatherBlendState` to clients automatically. If clients need it, add a replicated property on `AGameState` and update it from the `GameCoreEvent.Weather.StateChanged` subscription on the server.
- Does not query spatial data. Region boundary blending requires the area system to supply the alpha.
- Does not persist state. Server restarts recompute from the deterministic seed.
- Does not use Unreal `FTimerHandle` for event lifecycle — wall-clock comparisons via `FPlatformTime::Seconds()` avoid timer drift.
