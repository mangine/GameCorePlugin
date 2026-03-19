# Integration Guide

**Sub-page of:** [Time Weather System](../Time%20Weather%20System.md)

End-to-end setup, wiring, and usage samples.

---

## Module Setup

```csharp
// GameCore.Build.cs — no additional dependencies needed.
// TimeWeather lives entirely within the GameCore module.
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "GameplayTags",
    "Engine",
});
```

---

## Step 1 — Create a UTimeWeatherConfig Asset

1. Right-click in Content Browser → **GameCore → Time Weather Config**.
2. Set `DayDurationSeconds` (e.g. `1200` = 20-minute days).
3. Set `YearLengthDays` (e.g. `120` = 4 seasons × 30 days each).
4. Set `ServerEpochOffsetSeconds` to align day 0 with your desired calendar reference (or leave 0).
5. Set `WeatherSeedBase` to any fixed integer.
6. Assign a `UWeatherContextAsset` to `GlobalContext`.

Register the config in `DefaultGame.ini`:

```ini
[/Script/GameCore.TimeWeatherProjectSettings]
TimeWeatherConfig=/Game/Data/TimeWeather/DA_TimeWeatherConfig.DA_TimeWeatherConfig
```

---

## Step 2 — Create a Global UWeatherContextAsset

1. Right-click → **GameCore → Weather Context Asset**.
2. Add `USeasonDefinition` assets to the `Seasons` array in order (Spring, Summer, Autumn, Winter).
3. Assign a `UWeatherSequence` (Random or Graph) to each season.
4. Set `DefaultDayNightCurve` (a `UCurveFloat` with X=day time, Y=daylight).

**Minimal single-weather world (no seasons):**
- Leave `Seasons` empty.
- Assign a `UWeatherSequence_Random` with one entry to `DefaultWeatherSequence`.
- Assign a flat curve at Y=1 to `DefaultDayNightCurve`.

---

## Step 3 — Define Season Assets

```
DA_Season_Summer:
  SeasonTag = Season.Summer
  TransitionInPercent = 15
  DayNightCurve = <long-day curve>
  WeatherSequence = DA_Seq_Summer_Random
  TimedEvents:
    - Event = DA_Event_HeavyRain
      WindowStart = 0.5   // noon
      WindowEnd   = 0.75  // late afternoon
      DailyProbability = 0.35
```

---

## Step 4 — Define Weather Sequences

**Random with neighbour filter:**

```
DA_Seq_Summer_Random (UWeatherSequence_Random):
  KeyframesPerDay = 3
  Entries:
    - WeatherTag = Weather.Clear       Weight=3  ValidPredecessors=[]
    - WeatherTag = Weather.PartlyCloudy Weight=2  ValidPredecessors=[]
    - WeatherTag = Weather.Overcast     Weight=1  ValidPredecessors=[Weather.PartlyCloudy, Weather.Rain]
    - WeatherTag = Weather.Rain         Weight=1  ValidPredecessors=[Weather.Overcast]
      TransitionMin=120  TransitionMax=240
```

With this config, `Weather.Rain` can only follow `Weather.Overcast`, and `Weather.Overcast` can only follow `Weather.PartlyCloudy` or `Weather.Rain`. Clear skies never jump directly to rain.

---

## Step 5 — Add a Permanent Night Region (No Seasons)

```
DA_Context_Permafrost (UWeatherContextAsset):
  Seasons = []                         // empty = no season cycling
  DefaultDayNightCurve = DA_Curve_PermanentNight   // flat curve at Y=0
  DefaultWeatherSequence = DA_Seq_Blizzard_Simple
```

Region actor:

```cpp
UCLASS()
class APermafrostRegion : public AActor, public IWeatherContextProvider
{
    GENERATED_BODY()

    UPROPERTY(EditInstanceOnly)
    TObjectPtr<UWeatherContextAsset> ContextAsset;

    UPROPERTY(EditInstanceOnly)
    FGuid ContextIdGuid; // set once in editor, never changes

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

---

## Querying Weather State

```cpp
// On any server system — query global context:
if (auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>())
{
    FWeatherBlendState State = Sub->GetCurrentWeatherState();
    // State.BaseWeatherA, BaseWeatherB, BlendAlpha
    // State.OverlayWeather, OverlayAlpha
    // State.CurrentSeason
}

// Query a specific region:
FWeatherBlendState RegionState = Sub->GetCurrentWeatherState(MyRegionActor);
```

---

## Subscribing to Weather Events

```cpp
// Sea system subscribing to weather events to adjust wave simulation:
void USeaSimulationComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority()) return;

    if (auto* Bus = UGameCoreEventSubsystem::Get(this))
    {
        Bus->GetSubsystem<UGameplayMessageSubsystem>()
            ->RegisterListener<FWeatherEvent_EventActivated>(
                TAG_GameCoreEvent_Weather_EventActivated,
                this, &USeaSimulationComponent::OnWeatherEventActivated);
    }
}

void USeaSimulationComponent::OnWeatherEventActivated(
    FGameplayTag Channel, const FWeatherEvent_EventActivated& Msg)
{
    if (Msg.EventWeatherTag.MatchesTag(TAG_Weather_Storm))
        SetWaveIntensity(FMath::Lerp(BaseWaveIntensity, StormWaveIntensity,
            1.f)); // FadeIn handled by event — just react to activation
}
```

---

## Triggering an External Event

```cpp
// Boss ability triggers a storm:
void ABossActor::OnBossEnrage()
{
    if (auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>())
    {
        StormEventHandle = Sub->TriggerOverlayEvent(StormEventDefinition);
    }
}

// Boss dies — cancel it:
void ABossActor::OnBossDied()
{
    if (auto* Sub = GetWorld()->GetSubsystem<UTimeWeatherSubsystem>())
        Sub->CancelOverlayEvent(StormEventHandle);
}
```

---

## Querying Active Events (Cross-System)

```cpp
// Any system checks if any storm is currently active:
if (auto* Bus = UGameCoreEventSubsystem::Get(this))
{
    bool bStormActive = Bus->IsEventActive(TAG_Weather_Storm);
    // Returns true for Weather.Storm.Heavy, Weather.Storm.Light, etc.
}
```

---

## Getting Normalized Day Time on a Client

```cpp
// AGameState holds the replicated snapshot:
const FGameTimeSnapshot& Snap = GetGameState<AMyGameState>()->TimeSnapshot;
float NormTime = Snap.GetNormalizedDayTime((int64)FPlatformTime::Seconds());
float Daylight = MyDayNightCurve->GetFloatValue(NormTime);
```

---

## Gameplay Tags Required

Add to `DefaultGameplayTags.ini` (see [GMS Events](GMS%20Events.md) for the full list plus season and weather tags for your game):

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

; Season tags (game-defined)
+GameplayTagList=(Tag="Season.Spring")
+GameplayTagList=(Tag="Season.Summer")
+GameplayTagList=(Tag="Season.Autumn")
+GameplayTagList=(Tag="Season.Winter")
+GameplayTagList=(Tag="Season.Permafrost")

; Weather tags (game-defined — this system treats them as opaque tags)
+GameplayTagList=(Tag="Weather.Clear")
+GameplayTagList=(Tag="Weather.PartlyCloudy")
+GameplayTagList=(Tag="Weather.Overcast")
+GameplayTagList=(Tag="Weather.Rain")
+GameplayTagList=(Tag="Weather.Storm")
+GameplayTagList=(Tag="Weather.Storm.Heavy")
+GameplayTagList=(Tag="Weather.Fog")
```

**Note:** Weather tags are completely opaque to the Time Weather System — it never interprets them. Your VFX/audio systems decide what each tag means visually.

---

## What This System Does NOT Do

- Does not know what `Weather.Rain` looks like. That is the VFX system's problem.
- Does not replicate `FWeatherBlendState` to clients automatically. If you need client-side weather reactions, add a replicated property on `AGameState` and update it from the `GameCoreEvent.Weather.StateChanged` subscription on the server.
- Does not spatial-query which region a player is in. That is the area system's problem — it calls `GetCurrentWeatherState(RegionProvider)` and supplies `SpatialAlpha` to `GetBlendedWeatherState`.
- Does not manage any timers beyond the per-tick delta. All event lifecycle math uses wall-clock `FPlatformTime::Seconds()` comparisons, not Unreal timer handles, to avoid timer drift from frame rate variance.
