# Time Weather System — Architecture

**Part of:** GameCore Plugin | **Status:** Active Specification | **UE Version:** 5.7

The Time Weather System is a server-authoritative `UWorldSubsystem` that tracks in-game time, drives season progression, schedules and blends weather states, and manages active weather overlay events. It produces a single `FWeatherBlendState` output per registered weather context, consumed by downstream systems (VFX, audio, spawners, wave simulation) without those systems knowing anything about how weather is computed.

---

## Dependencies

### Unreal Engine Modules
| Module | Use |
|---|---|
| `Engine` | `UWorldSubsystem`, `UDataAsset`, `UCurveFloat`, `AGameStateBase` |
| `GameplayTags` | `FGameplayTag`, `FGameplayTagContainer` |
| `DeveloperSettings` | `UDeveloperSettings` for `UTimeWeatherProjectSettings` |
| `CoreUObject` | `UObject`, base USTRUCT/UCLASS machinery |
| `NetCore` / `Engine` | `DOREPLIFETIME`, replication of `FGameTimeSnapshot` via `AGameState` |

### GameCore Plugin Systems
| System | Use |
|---|---|
| **Event Bus (`UGameCoreEventSubsystem`)** | Broadcasting all GMS time/weather events; active event registry (`RegisterActiveEvent` / `UnregisterActiveEvent` / `IsEventActive`) |

### No Hard Dependencies On
- Serialization / Persistence System — weather state is fully deterministic and never persisted.
- Requirement System — no requirements gating weather changes.
- Interaction System, State Machine, Progression — zero coupling; all of these may _listen_ to weather events via GMS.

---

## Requirements

| ID | Requirement |
|---|---|
| R1 | Server-authoritative time tracking. Clients reconstruct current time from a replicated snapshot — no per-second replication. |
| R2 | Time is deterministic from real-world Unix time + config. No persistence needed; the server recomputes on restart. |
| R3 | A global `UWeatherContextAsset` provides default time, season, and weather for the entire world. |
| R4 | Regions may supply their own `UWeatherContextAsset` via `IWeatherContextProvider`. Region contexts advance independently from the global context. |
| R5 | Seasons are defined as ordered arrays of `USeasonDefinition` assets. Each season has a Gameplay Tag queryable by other systems. |
| R6 | Seasons support configurable blend-in transitions (0–50% of season duration) from the prior season. |
| R7 | Regions with no season array (e.g. permafrost, permanent night) use a flat `DayNightCurve` and a standalone `UWeatherSequence`. |
| R8 | Weather sequences are abstract: implementations include weighted-random-with-predecessor-filter and state-machine graph. |
| R9 | Weather state is expressed as `FWeatherBlendState`: BaseA tag, BaseB tag, blend alpha, optional overlay tag, overlay alpha, plus season context. |
| R10 | Weather blending is computed once per in-game day (daily schedule). Per-tick cost is alpha interpolation only — no asset queries per tick. |
| R11 | Overlay weather events have lifecycle phases: fade-in, sustain, fade-out. Alpha is computed per tick from wall-clock elapsed time. |
| R12 | Events can be scheduled probabilistically per season or triggered externally via `TriggerOverlayEvent`. |
| R13 | Concurrent overlay events on the same context: highest `Priority` wins. Equal priority: first-registered wins. Lower-priority events queue and activate when the higher-priority event completes. |
| R14 | Active weather events are registered into the shared `UGameCoreEventSubsystem` active event registry for cross-system queries. |
| R15 | Region boundary blending (`FRegionWeatherBlend`) is a future integration point. A static helper `GetBlendedWeatherState` is exposed but the area system drives spatial alpha. |
| R16 | GMS events fire on significant transitions only: dawn, dusk, day rollover, season change, weather state change, event activated/completed. |
| R17 | The system has zero knowledge of VFX, audio, UI, or rendering. Its only output is `FWeatherBlendState` and GMS events. |

---

## Features

- **Deterministic time** — derived from `UnixTime - EpochOffset`. No save required; fully reproducible after server restart.
- **Per-context weather** — global world context plus any number of region overrides, each advancing independently.
- **Season cycling** — ordered `USeasonDefinition` array with configurable blend-in transitions.
- **Daily weather schedule** — computed once at dawn per context; tick cost is a single lerp.
- **Two sequence strategies** — `UWeatherSequence_Random` (weighted with predecessor filter) and `UWeatherSequence_Graph` (explicit state-machine).
- **Overlay event system** — fade-in/sustain/fade-out lifecycle, priority queue, graceful cancel with proportional fade-out.
- **Cross-system event registry** — `UGameCoreEventSubsystem::IsEventActive(Tag)` lets any system query active weather without subscribing.
- **GMS broadcast** — fire-and-forget notifications on meaningful state changes; alpha changes never fire events.
- **Region boundary blend helper** — `GetBlendedWeatherState(A, B, Alpha)` static for area system integration.
- **Client time sync** — `FGameTimeSnapshot` replicated via `AGameState` once per day; clients derive local time with no server callbacks.

---

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Time storage | Derived from `UnixTime - EpochOffset` | Zero persistence; fully deterministic across server restarts |
| Client time sync | Replicate `FGameTimeSnapshot` once; clients derive locally | No per-second events; snapshot is stateless config |
| Weather output | Single `FWeatherBlendState` struct | Downstream systems receive one typed struct, never touch sequence logic |
| Weather scheduling | One `FDailyWeatherSchedule` built at dawn per context | All blend keyframes computed once; tick cost is lerp only |
| Overlay priority | Highest priority wins; equal priority = first-registered wins; lower-priority queues | Simple, designer-predictable, no arbitrary blending of incompatible event weathers |
| Weather sequences | Abstract `UWeatherSequence` with two concrete types | Random arrays suit simple regions; graph suits complex biomes — same interface, no subsystem changes |
| Predecessor filter | `ValidPredecessors` array on `UWeatherSequence_Random` entries | Constraints on incoming entry; adding new weathers doesn't require editing existing ones |
| Seed strategy | `Hash(WeatherSeedBase, Day, ContextId)` | Reproducible per day per region; no saved state |
| Region independence | Each `IWeatherContextProvider` maintains its own `FContextState` | Permafrost region can be permanent night while global world is in summer |
| Region boundary blend | `FRegionWeatherBlend` + `GetBlendedWeatherState()` helper | Area system drives spatial alpha; this system stays spatially unaware |
| Active event registry | Thin registry on `UGameCoreEventSubsystem` | Cross-system queryability without coupling; weather populates as a side effect |
| No persistence | Intentional | Real-time derivation is simpler and sufficient; weather re-randomises from deterministic seed on restart |
| No VFX/audio | Intentional | Downstream systems own execution; this system owns only state computation |
| Event durations in real-time | Intentional | A 5-minute storm is 5 minutes for players regardless of day speed |
| `ResolveSeasonContext` in query path | `const_cast` accepted | No mutation occurs; alternatively split into a pure const variant |

---

## Logic Flow

### Initialisation
```
UTimeWeatherSubsystem::Initialize()
  └─ Load UTimeWeatherConfig via UTimeWeatherProjectSettings
  └─ Populate FGameTimeSnapshot from config
  └─ InitGlobalContext()
       └─ Add FContextState for FGuid() (global)
       └─ RebuildContextCaches()  ← ComputeSeasonRanges
       └─ RebuildDailySchedule()  ← build keyframes + roll timed events
  └─ PushSnapshotToClients()  ← set AMyGameState::TimeSnapshot, ForceNetUpdate
```

### Region Registration
```
ARegionActor::BeginPlay()  [server]
  └─ UTimeWeatherSubsystem::RegisterContextProvider(this)
       └─ Validate GUID uniqueness
       └─ Add FContextState keyed by GetContextId()
       └─ RebuildContextCaches()
       └─ RebuildDailySchedule()
```

### Per-Tick Flow
```
UTimeWeatherSubsystem::Tick(DeltaTime)
  ├─ Compute NormDayTime, CurrentDay from FPlatformTime::Seconds()
  ├─ [If day rolled over]
  │    ├─ For each FContextState: RebuildDailySchedule()
  │    ├─ BroadcastTimeEvents()  ← DayRolledOver GMS
  │    └─ PushSnapshotToClients()
  └─ For each FContextState: TickContextState()
       ├─ AdvanceBaseBlend()       ← find active keyframe, lerp BlendAlpha
       ├─ TickOverlayEvents()      ← TickAlpha per event, expire & promote queued
       ├─ TickScheduledTriggers()  ← fire probabilistic events at NormTime threshold
       ├─ Update SeasonBlendAlpha  ← continuous lerp during blend-in window
       └─ BroadcastWeatherChanged() [if tags changed]
  └─ BroadcastTimeEvents()  ← Dawn/Dusk once-per-day per context
```

### Daily Schedule Construction
```
RebuildDailySchedule(State, Day)
  └─ ResolveSeasonContext()   ← season ranges lookup by DayOfYear
  └─ BroadcastSeasonChanged() [if season tag changed]
  └─ ResolveSequence()        ← season WeatherSequence → context default
  └─ MakeSeededStream()       ← Hash(SeedBase, Day, ContextId)
  └─ For each keyframe slot (0..N-1):
       └─ UWeatherSequence::GetNextWeather()   ← deterministic, seeded
       └─ Build FDailyWeatherKeyframe
  └─ RollTimedEvents()        ← probability rolls, produce FScheduledEventTrigger[]
```

### Overlay Event Lifecycle
```
TriggerOverlayEvent(EventDef, Provider)
  ├─ Find or validate FContextState
  ├─ If higher-or-equal-priority active → Insert into QueuedEvents (sorted)
  └─ Else → RegisterInEventRegistry() → BroadcastEventActivated() → ActiveEvents[0]

TickOverlayEvents()
  ├─ TickAlpha() per active event (fade-in / sustain / fade-out)
  ├─ On expire: BroadcastEventCompleted() → UnregisterFromEventRegistry()
  │             ActivateQueuedEvent()     ← reset timers to Now
  └─ Write top ActiveEvent → Blend.OverlayWeather / OverlayAlpha

CancelOverlayEvent(EventId)
  ├─ Active → clamp SustainEnd=Now, FadeOutEnd=Now+ProportionalFade
  └─ Queued → remove immediately
```

---

## Known Issues

| ID | Issue | Status |
|---|---|---|
| KI-1 | `PushSnapshotToClients` casts to `AMyGameState` — couples the plugin to the game module's GameState subclass. The game module must supply this type. | Accepted; document the required interface contract. |
| KI-2 | `GetDaylightIntensity` calls `ResolveSeasonContext` via `const_cast` because `FContextState` is non-const. The method performs no mutation. | Accepted workaround; or split `ResolveSeasonContext` into a const path. |
| KI-3 | `FRegionWeatherBlend::GetBlendedWeatherState` tag blending is winner-takes-all at 0.5 alpha. A player standing exactly on the boundary flips tags abruptly. | Known limitation; area system should supply smooth alpha and use hysteresis. |
| KI-4 | `FScheduledEventTrigger` triggers fire based on `NormDayTime` threshold, which advances monotonically. If the server hitches across a trigger window, the event fires late rather than at the authored time. | Acceptable; event scheduling tolerance is ~1 tick. |
| KI-5 | `ActivateQueuedEvent` resets all timers to `Now` — a queued event that waited a long time loses none of its authored duration, which is correct but could surprise designers if many events queue. | Intentional; document clearly. |
| KI-6 | Dawn/Dusk events fire per-context but the broadcast payload `FTimeEvent_DawnDusk` includes a `FGameplayTag ContextId` field that is never populated (it carries a tag, not a GUID). | Fix: carry `FGuid ContextId` matching the other payloads, or remove the field and make Dawn/Dusk global-only. |

---

## File Structure

```
GameCore/Source/GameCore/TimeWeather/
  TimeWeatherTypes.h              — FGameTimeSnapshot, FSeasonContext, FWeatherBlendState,
                                    FDailyWeatherKeyframe, FDailyWeatherSchedule,
                                    FActiveWeatherEvent, FScheduledEventTrigger,
                                    FRegionWeatherBlend, FSeasonRange
  TimeWeatherEventMessages.h      — GMS payload structs, channel tag constants
  WeatherContextProvider.h        — IWeatherContextProvider interface
  TimeWeatherConfig.h             — UTimeWeatherProjectSettings, UTimeWeatherConfig
  WeatherContextAsset.h/.cpp      — UWeatherContextAsset
  SeasonDefinition.h/.cpp         — USeasonDefinition, FSeasonWeatherEvent
  WeatherEventDefinition.h/.cpp   — UWeatherEventDefinition
  WeatherSequence.h               — UWeatherSequence (abstract)
  WeatherSequence_Random.h/.cpp   — UWeatherSequence_Random, FWeightedWeather
  WeatherSequence_Graph.h/.cpp    — UWeatherSequence_Graph, FWeatherGraphNode, FWeatherGraphEdge
  TimeWeatherSubsystem.h/.cpp     — UTimeWeatherSubsystem
```

All files live in the `GameCore` runtime module. No editor-only module is required.
