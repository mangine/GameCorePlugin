# Time Weather System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Time Weather System is a server-authoritative `UWorldSubsystem` that tracks in-game time, drives season progression, schedules and blends weather states, and manages active weather events. It produces a single `FWeatherBlendState` output per weather context consumed by downstream systems (VFX, audio, spawners, wave simulation, etc.) without those systems knowing anything about how weather is computed.

---

## Sub-Pages

- [Data Assets](Time%20Weather%20System/Data%20Assets.md) — `UTimeWeatherConfig`, `UWeatherContextAsset`, `USeasonDefinition`, `UWeatherEventDefinition`
- [Weather Sequences](Time%20Weather%20System/Weather%20Sequences.md) — `UWeatherSequence`, `UWeatherSequence_Random`, `UWeatherSequence_Graph`
- [Core Types](Time%20Weather%20System/Core%20Types.md) — `FGameTimeSnapshot`, `FWeatherBlendState`, `FActiveWeatherEvent`, `FDailyWeatherSchedule`, `IWeatherContextProvider`
- [UTimeWeatherSubsystem](Time%20Weather%20System/UTimeWeatherSubsystem.md) — authority subsystem: time tick, season, weather scheduling, event management
- [Active Event Registry](Time%20Weather%20System/Active%20Event%20Registry.md) — lightweight tag-keyed event registry on `UGameCoreEventSubsystem`
- [GMS Events](Time%20Weather%20System/GMS%20Events.md) — all GMS channel tags, payload structs, scopes
- [Integration Guide](Time%20Weather%20System/Integration%20Guide.md) — setup, wiring, sample code

---

## Requirements

| ID | Requirement |
|---|---|
| R1 | Server-authoritative time tracking. Clients reconstruct current time from a replicated snapshot — no per-second replication. |
| R2 | Time is deterministic from real-world Unix time + config. No persistence needed; the server recomputes on restart. |
| R3 | A global `UWeatherContextAsset` provides default time, season, and weather for the entire world. |
| R4 | Regions may supply their own `UWeatherContextAsset` via `IWeatherContextProvider`. Region contexts advance independently from global. |
| R5 | Seasons are defined as ordered arrays of `USeasonDefinition` assets. Each season has a Gameplay Tag queryable by other systems. |
| R6 | Seasons support configurable blend-in transitions (0–50% of season duration) from the prior season. |
| R7 | Regions with no season array (e.g. permafrost, permanent night) use a flat `DayNightCurve` and a standalone `UWeatherSequence`. |
| R8 | Weather sequences are abstract: implementations include weighted-random-with-neighbour-filter and state-machine graph. |
| R9 | Weather state is expressed as `FWeatherBlendState`: BaseA tag, BaseB tag, blend alpha, optional overlay tag, overlay alpha, plus season context. |
| R10 | Weather blending is computed once per in-game day (daily schedule). Per-tick cost is alpha interpolation only — no asset queries per tick. |
| R11 | Overlay weather events have lifecycle phases: fade-in, sustain, fade-out. Alpha is computed per tick from wall-clock elapsed time. |
| R12 | Events can be scheduled probabilistically per season or triggered externally via `TriggerOverlayEvent`. |
| R13 | Concurrent overlay events on the same context: highest `Priority` wins. Equal priority: first-registered wins. Lower-priority events queue and activate when the higher-priority event completes. |
| R14 | Active weather events are registered into a shared lightweight tag-keyed event registry on `UGameCoreEventSubsystem` for cross-system queries. |
| R15 | Region boundary blending (`FRegionWeatherBlend`) is a future integration point. A helper `GetBlendedWeatherState` is exposed but the area system drives spatial alpha. |
| R16 | GMS events fire on significant transitions only: dawn, dusk, day rollover, season change, weather state change, event activated/completed. |
| R17 | The system has zero knowledge of VFX, audio, UI, or rendering. Its only output is `FWeatherBlendState` and GMS events. |

---

## Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Time storage | Derived from `UnixTime - EpochOffset` | Zero persistence; fully deterministic across server restarts |
| Client time sync | Replicate `FGameTimeSnapshot` once; clients derive locally | No per-second events; snapshot is stateless config |
| Weather output | Single `FWeatherBlendState` struct | Downstream systems receive one typed struct, never touch sequence logic |
| Weather scheduling | One `FDailyWeatherSchedule` built at dawn | All blend keyframes computed once; tick cost is lerp only |
| Overlay priority | Highest priority wins; equal priority = first-registered wins; queue lower | Simple, designer-predictable, no arbitrary blending of incompatible event weathers |
| Weather sequences | Abstract `UWeatherSequence` with two concrete types | Random arrays suit simple regions; graph suits complex biomes — same interface, no subsystem changes |
| Neighbour filter | `ValidFollowers` array on `UWeatherSequence_Random` entries | Gives "rainy → cloudy, not sunny" smoothness without a full graph |
| Seed strategy | `Hash(WeatherSeedBase, Day, ContextId)` | Reproducible per day per region; no saved state |
| Region independence | Each `IWeatherContextProvider` maintains its own context state | Permafrost region can be in permanent night while global world is in summer |
| Region boundary blend | `FRegionWeatherBlend` + `GetBlendedWeatherState()` helper | Area system drives spatial alpha; this system stays spatially unaware |
| Active event registry | Thin registry on `UGameCoreEventSubsystem` | Cross-system queryability without coupling; weather populates as side effect |
| No persistence | Intentional | Real-time derivation is simpler and sufficient; weather state re-randomises on restart from deterministic seed |
| No VFX/audio | Intentional | Downstream systems own execution; this system owns only state computation |

---

## File Structure

```
GameCore/Source/GameCore/
  TimeWeather/
    TimeWeatherSubsystem.h / .cpp
    TimeWeatherConfig.h
    WeatherContextAsset.h / .cpp
    SeasonDefinition.h / .cpp
    WeatherEventDefinition.h / .cpp
    WeatherSequence.h                  <- abstract base
    WeatherSequence_Random.h / .cpp
    WeatherSequence_Graph.h / .cpp
    WeatherContextProvider.h           <- IWeatherContextProvider
    TimeWeatherTypes.h                 <- FGameTimeSnapshot, FWeatherBlendState,
                                          FActiveWeatherEvent, FDailyWeatherSchedule,
                                          FRegionWeatherBlend, FSeasonContext
    TimeWeatherEventMessages.h         <- all GMS payload structs
```
