# Time Weather System — Usage

**Part of:** GameCore Plugin | **Status:** Active Specification | **UE Version:** 5.7

End-to-end setup and usage samples.

---

## Module Setup

```csharp
// YourGame.Build.cs
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "GameplayTags",
    "Engine",
});
```

---

## Step 1 — Project Settings

Create a `UTimeWeatherConfig` data asset and register it in `DefaultGame.ini`:

```ini
[/Script/GameCore.TimeWeatherProjectSettings]
TimeWeatherConfig=/Game/Data/TimeWeather/DA_TimeWeatherConfig.DA_TimeWeatherConfig
```

The subsystem reads this at `Initialize` via `GetDefault<UTimeWeatherProjectSettings>()`.

---

## Step 2 — UTimeWeatherConfig Asset

Right-click in Content Browser → **GameCore → Time Weather Config**.

| Property | Example Value | Notes |
|---|---|---|
| `ServerEpochOffsetSeconds` | `1735689600` | 2025-01-01 UTC = in-game day 0 |
| `DayDurationSeconds` | `1200` | 20-minute days |
| `YearLengthDays` | `120` | 4 seasons × 30 days |
| `WeatherSeedBase` | `7331` | Change to globally reseed all weather |
| `GlobalContext` | `DA_Context_World` | Must not be null |
| `DawnThreshold` | `0.25` | Quarter-day |
| `DuskThreshold` | `0.75` | Three-quarter-day |

`IsDataValid` errors in-editor if `GlobalContext` is null or thresholds are invalid.

---

## Step 3 — AGameState Subclass (Required)

The subsystem replicates `FGameTimeSnapshot` via `AGameState`. The game module must provide this subclass:

```cpp
// MyGameState.h
UCLASS()
class AMyGameState : public AGameState
{
    GENERATED_BODY()
public:
    // Updated once per day by UTimeWeatherSubsystem.
    UPROPERTY(ReplicatedUsing=OnRep_TimeSnapshot)
    FGameTimeSnapshot TimeSnapshot;

    UFUNCTION()
    void OnRep_TimeSnapshot()
    {
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

Ensure `DefaultEngine.ini` references your subclass:

```ini
[/Script/Engine.GameMode]
GameStateClass=/Script/MyGame.MyGameState
```

---

## Step 4 — Global Context Asset

Create `UWeatherContextAsset` at e.g. `/Game/Data/TimeWeather/DA_Context_World`.

- Add four `USeasonDefinition` assets to `Seasons` in order: Spring, Summer, Autumn, Winter.
- Assign a `UCurveFloat` to `DefaultDayNightCurve` (X = normalised day time [0,1), Y = daylight [0,1]).
- Leave `DaysPerSeason = 0` to split `YearLengthDays` equally.

**Minimal single-weather world (no seasons):**
- Leave `Seasons` empty.
- Assign a `UWeatherSequence_Random` with one entry to `DefaultWeatherSequence`.
- Assign a flat curve at `Y = 1` to `DefaultDayNightCurve`.

---

## Step 5 — Season Assets

```
DA_Season_Summer:
  SeasonTag            = Season.Summer
  TransitionInPercent  = 15          // first 15% of Summer blends in from Spring
  DayNightCurve        = Curve_LongDay
  WeatherSequence      = DA_Seq_Summer
  TimedEvents:
    [0] Event=DA_Event_HeavyRain    WindowStart=0.5  WindowEnd=0.75  DailyProbability=0.35
    [1] Event=DA_Event_Thunderstorm WindowStart=0.6  WindowEnd=0.8   DailyProbability=0.10
```

---

## Step 6 — Weather Sequences

**Random with predecessor filter (recommended for most regions):**

```
DA_Seq_Summer (UWeatherSequence_Random):
  KeyframesPerDay = 3
  Entries:
    { WeatherTag=Weather.Clear,        Weight=3, ValidPredecessors=[] }
    { WeatherTag=Weather.PartlyCloudy, Weight=2, ValidPredecessors=[] }
    { WeatherTag=Weather.Overcast,     Weight=1,
      ValidPredecessors=[Weather.PartlyCloudy, Weather.Rain],
      TransitionMin=120, TransitionMax=240 }
    { WeatherTag=Weather.Rain,         Weight=1,
      ValidPredecessors=[Weather.Overcast],
      TransitionMin=180, TransitionMax=360 }
```

With this config: Rain can only follow Overcast; Overcast can only follow PartlyCloudy or Rain. Clear skies never jump directly to Rain.

**Graph-based sequence (tight biome control):**

```
DA_Seq_BossArena (UWeatherSequence_Graph):
  InitialWeather = Weather.Overcast
  KeyframesPerDay = 2
  Nodes:
    Weather.Overcast:
      Edges: [ { To=Weather.Storm, Weight=1, SeasonFilter=Season.Winter } ]
    Weather.Storm:
      Edges: [ { To=Weather.Overcast, Weight=1 } ]
```

---

## Step 7 — Permanent Night / Seasonless Region

```
DA_Context_Permafrost:
  Seasons              = []
  DefaultDayNightCurve = DA_Curve_PermanentNight   // flat Y=0
  DefaultWeatherSequence = DA_Seq_Blizzard
```

Region actor implementing `IWeatherContextProvider`:

```cpp
UCLASS()
class APermafrostRegion : public AActor, public IWeatherContextProvider
{
    GENERATED_BODY()

    // Set once in the editor per placed instance. NEVER generate at runtime.
    UPROPERTY(EditInstanceOnly, Category="TimeWeather")
    FGuid ContextIdGuid;

    UPROPERTY(EditInstanceOnly, Category="TimeWeather")
    TObjectPtr<UWeatherContextAsset> ContextAsset;

public:
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

**Editor workflow:** After placing the actor, use a `CallInEditor` helper or the UE GUID generator to assign a stable GUID and save the level. Never regenerate at runtime — that breaks seed determinism.

---

## Querying Weather State (Server)

```cpp
auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>();

// Global context:
FWeatherBlendState State = Sub->GetCurrentWeatherState();
// State.BaseWeatherA, BaseWeatherB, BlendAlpha  — lerp VFX params
// State.OverlayWeather, OverlayAlpha            — apply storm/fog overlay
// State.CurrentSeason                           — foliage, spawn tables, etc.

// Specific region:
FWeatherBlendState RegionState = Sub->GetCurrentWeatherState(MyRegionActor);

// Daylight intensity:
float Light = Sub->GetDaylightIntensity(); // [0,1]

// Current season tag:
FGameplayTag Season = Sub->GetCurrentSeasonTag();
```

---

## Subscribing to Weather Events (Server)

```cpp
void UWaveSimComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority()) return;

    auto* GMS = GetWorld()->GetSubsystem<UGameplayMessageSubsystem>();
    GMS->RegisterListener<FWeatherEvent_EventActivated>(
        TAG_GameCoreEvent_Weather_EventActivated,
        this, &UWaveSimComponent::OnWeatherEventActivated);
}

void UWaveSimComponent::OnWeatherEventActivated(
    FGameplayTag, const FWeatherEvent_EventActivated& Msg)
{
    if (Msg.EventWeatherTag.MatchesTag(TAG_Weather_Storm))
        SetTargetWaveIntensity(StormWaveIntensity);
        // Blend over Msg.FadeInSeconds in your component's tick.
}
```

---

## Triggering and Cancelling an External Event

```cpp
// Boss enrages — trigger storm on global context.
void ABossActor::OnEnrage()
{
    auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>();
    StormHandle = Sub->TriggerOverlayEvent(StormEventDef); // nullptr = global
}

// Boss dies — proportional fade-out.
void ABossActor::OnDeath()
{
    auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>();
    Sub->CancelOverlayEvent(StormHandle);
}
```

---

## Cross-System Event Query

```cpp
// Is any storm active on any context?
auto* Bus = UGameCoreEventSubsystem::Get(this);
bool bStorm = Bus->IsEventActive(TAG_Weather_Storm);
// Matches Weather.Storm, Weather.Storm.Heavy, Weather.Storm.Light (tag hierarchy).

// All active storm event GUIDs:
TArray<FGuid> Storms = Bus->GetActiveEvents(TAG_Weather_Storm);
```

---

## Getting Normalised Day Time on a Client

```cpp
// Any client-side actor / component:
if (AMyGameState* GS = GetWorld()->GetGameState<AMyGameState>())
{
    const FGameTimeSnapshot& Snap = GS->TimeSnapshot;
    float NormTime = Snap.GetNormalizedDayTime(FPlatformTime::Seconds());
    float Daylight = MyDayNightCurve->GetFloatValue(NormTime);
    // Drive sky material parameter, post-process exposure, etc.
}
```

---

## Region Boundary Blending (Future / Area System)

```cpp
// Area system computes SpatialAlpha [0,1] for the player's position, then:
FWeatherBlendState Merged = UTimeWeatherSubsystem::GetBlendedWeatherState(
    Sub->GetCurrentWeatherState(RegionA),
    Sub->GetCurrentWeatherState(RegionB),
    SpatialAlpha);
// Feed Merged into VFX/audio systems.
```

---

## Required Gameplay Tags

Add to `DefaultGameplayTags.ini`:

```ini
; Time events
+GameplayTagList=(Tag="GameCoreEvent.Time.DawnBegan")
+GameplayTagList=(Tag="GameCoreEvent.Time.DuskBegan")
+GameplayTagList=(Tag="GameCoreEvent.Time.DayRolledOver")
+GameplayTagList=(Tag="GameCoreEvent.Time.SeasonChanged")

; Weather events
+GameplayTagList=(Tag="GameCoreEvent.Weather.StateChanged")
+GameplayTagList=(Tag="GameCoreEvent.Weather.EventActivated")
+GameplayTagList=(Tag="GameCoreEvent.Weather.EventCompleted")

; Season tags (game-defined, opaque to this system)
+GameplayTagList=(Tag="Season.Spring")
+GameplayTagList=(Tag="Season.Summer")
+GameplayTagList=(Tag="Season.Autumn")
+GameplayTagList=(Tag="Season.Winter")
+GameplayTagList=(Tag="Season.Permafrost")

; Weather state tags (game-defined, opaque to this system)
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

Weather and season tags are **completely opaque** to the Time Weather System.

---

## What This System Does NOT Do

- Does **not** render anything — no sky, no particles, no post-process.
- Does **not** automatically replicate `FWeatherBlendState` to clients. Add a replicated `AGameState` property and update it from the `GameCoreEvent.Weather.StateChanged` subscription on the server if clients require it.
- Does **not** query spatial data — region boundary blending requires the area system to supply the alpha.
- Does **not** persist state — server restarts recompute from the deterministic seed.
- Does **not** use `FTimerHandle` for event lifecycle — wall-clock comparisons via `FPlatformTime::Seconds()` avoid timer drift.
