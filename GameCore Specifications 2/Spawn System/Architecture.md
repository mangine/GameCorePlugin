# Spawn System — Architecture

**Part of:** GameCore Plugin | **Module:** `GameCore` | **UE Version:** 5.7

The Spawn System continuously populates the world with entities (NPCs, mobs, ambient creatures, etc.) without coupling to any specific actor class or game-module concept. A single `USpawnManagerComponent` placed on a lightweight anchor actor drives all spawn logic for one location: async class loading, flow-timer scheduling, per-entry requirement evaluation, spawn-point resolution, live-instance tracking, and optional player-density-based interval scaling.

---

## Dependencies

### Unreal Engine Modules

| Module | Usage |
|---|---|
| `Engine` | `UActorComponent`, `FTimerManager`, `UAssetManager`, `FStreamableManager` |
| `NavigationSystem` | `UNavigationSystemV1::ProjectPointToNavigation` (RadiusRandom strategy) |
| `GameplayTags` | Requirement list assets may filter on gameplay tags |

### GameCore Plugin Systems

| System | Usage |
|---|---|
| **Requirement System** | `URequirementList` / `FRequirementContext` — gates per-entry spawn eligibility |
| **Loot Table System** | `ULootTable` — soft-reference override passed to entities post-spawn; spawner does not load or use it |

> The Spawn System has **no dependency** on the Event Bus, State Machine, Serialization, Backend, or any other GameCore system. It is one of the simplest standalone systems in the plugin.

### Game Module (injected, not a compile-time dependency)

| Injection point | Purpose |
|---|---|
| `USpawnManagerComponent::OnCountNearbyPlayers` | `TFunction<int32(FVector, float)>` — returns the number of players within a radius of the anchor. Bound by the game module at world start. Not required; scaling is skipped and a one-time warning is logged if unbound. |

---

## Requirements

| # | Requirement |
|---|---|
| R1 | The component must manage multiple entity types simultaneously, each with its own max live count. |
| R2 | A global flow timer controls how often the system attempts to spawn. The base interval must always be >= 10 s at runtime. |
| R3 | A global `FlowCount` caps the total number of spawns per flow tick across all entries. |
| R4 | Each entry may optionally declare a `MaxPerTick` cap limiting how many of that specific type spawn per tick. |
| R5 | A `bScaleByNearbyPlayers` flag enables dynamic interval scaling. Player count is sampled once at each flow tick expiry. Interval lerps from `BaseFlowInterval` down to `MinFlowInterval` as player count increases. Both bounds are >= 10 s. |
| R6 | A jitter of `[0, 1]` seconds is added to every rescheduled timer to spread load across many simultaneous spawn managers. |
| R7 | The system must not couple to any specific entity class. Entities are identified via the `ISpawnableEntity` interface. |
| R8 | Each `FSpawnEntry` may reference a `URequirementList` that gates whether that entry is eligible to spawn on a given tick (e.g. time-of-day, world state). The context is world-state only — no `PlayerState`. |
| R9 | Spawn location is resolved by a pluggable `USpawnPointConfig` strategy object (instanced on the component). Two concrete strategies ship: `USpawnPointConfig_RadiusRandom` and `USpawnPointConfig_PointList`. |
| R10 | Spawn point candidates are child `USceneComponent`s on the anchor actor identified by tag — not mesh sockets. |
| R11 | When a live entity is destroyed, its slot becomes vacant immediately via `OnDestroyed` binding. Weak pointer pruning at each flow tick is a safety net only. |
| R12 | Spawn failures (navmesh miss, collision block) are silent skips. The slot retries on the next natural flow tick. No dedicated retry timer. |
| R13 | Entity classes are `TSoftClassPtr`. Async load is queued on first encounter; entries not yet loaded are skipped on the current tick. No blocking load on the game thread. |
| R14 | Each `FSpawnEntry` may optionally hold a `TSoftObjectPtr<ULootTable>` override. The spawner passes this to the entity post-spawn via `OnSpawnedByManager`; it does not load or use it itself. |
| R15 | The system is server-only. `BeginPlay` returns immediately if `!HasAuthority`. No replication, no client path. |
| R16 | Spawn state (`LiveInstances`) is fully ephemeral and never persisted. Server restart repopulates naturally within the first few flow ticks. |
| R17 | Player counting is injected via a `TFunction` delegate (`OnCountNearbyPlayers`) bound by the game module. If unbound and `bScaleByNearbyPlayers` is true, scaling is skipped and a one-time warning is logged. |

---

## Features

- **Multi-type population** — one component manages N distinct entity types simultaneously.
- **Flow budget** — global `FlowCount` prevents any single entry from monopolizing a tick; `MaxPerTick` per entry provides fine-grained throttle.
- **Async class loading** — `TSoftClassPtr` assets load asynchronously; no game-thread stalls.
- **Pluggable spawn location** — `USpawnPointConfig` hierarchy allows any placement strategy without touching the component.
- **Requirement-gated entries** — time-of-day, weather, quest phase, etc. can gate individual entry eligibility per tick.
- **Density-aware scheduling** — optional player-count-based interval scaling with a guaranteed 10 s floor.
- **Jitter** — randomised `[0, 1]` s offset on every timer reschedule prevents simultaneous tick spikes.
- **Deferred spawn** — `bDeferConstruction` ensures `OnDestroyed` is bound before entity `BeginPlay` fires.
- **Loot override passthrough** — spawner is a transparent channel; it passes a soft loot ref to the entity without loading it.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| `ISpawnableEntity` marker interface, not a base class | Any actor class regardless of hierarchy can be made spawn-eligible with one line. Avoids forcing a common base that would conflict with existing actor hierarchies. |
| `USpawnPointConfig` as an instanced `UObject` hierarchy | Strategy objects keep each placement variant's properties and logic self-contained. Adding new strategies requires zero changes to the component. |
| Child `USceneComponent` tags, not root mesh sockets | Socket points require a mesh on the anchor actor and artist cooperation. Scene component tags are 100% designer-driven with no code changes. |
| Global `FlowCount` budget across all entries | Prevents a single entry with many vacancies from monopolizing spawn budget. Entry ordering is implicit priority. |
| Reschedule timer before spawn work | Spawn attempts can be slow (async loads, nav queries). Rescheduling first ensures the timer period is not skewed by work duration. |
| Spawn failure = silent skip, retry next natural tick | A dedicated retry timer risks hammering a bad location every few frames. Natural spacing is safe and sufficient. |
| `OnDestroyed` binding + weak pointer prune as safety net | `OnDestroyed` handles the common case with zero polling. Weak pointer pruning on each tick catches actors destroyed via paths that bypass the delegate (e.g. world flush). |
| Per-entry `URequirementList`, not inline requirements | Shared requirement list assets let multiple entries reference the same condition without duplication. Consistent with how requirements are used across all GameCore systems. |
| `OnCountNearbyPlayers` as injected delegate | GameCore cannot assume how the game represents players. Delegate pattern keeps the system reusable across projects. |
| `MinFlowInterval` hard-clamped to >= 10 s at runtime | Designer input is never trusted blindly. The floor prevents near-zero intervals that would thrash the spawn system under high player load. |
| No persistence | Spawn state is intentionally ephemeral. NPCs and mobs are respawnable world state and must not register with `UPersistenceSubsystem`. |
| `GetLootTableOverrideForClass` on the component | Entities call this from `OnSpawnedByManager` to receive their override. Keeps the data flow clean: spawner owns the override config, entity applies it. |

---

## Logic Flow

```
BeginPlay (server only — returns early if !HasAuthority)
  ├─ Validate SpawnPointConfig
  │    └─ null → create USpawnPointConfig_RadiusRandom (Radius=500) + UE_LOG Warning
  ├─ [!SHIPPING] ValidateRequirements on each entry's SpawnRequirements
  ├─ RequestAsyncClassLoads()
  │    └─ Collect unloaded TSoftClassPtr paths → UAssetManager::GetStreamableManager().RequestAsyncLoad
  └─ ScheduleNextFlowTick(NearbyPlayers=0)
       └─ SetTimer(FlowTimerHandle, Interval = ComputeNextInterval(0), bLoop=false)

OnFlowTimerExpired
  ├─ NearbyPlayers = GetNearbyPlayerCount()    [if bScaleByNearbyPlayers]
  ├─ ScheduleNextFlowTick(NearbyPlayers)        ← RESCHEDULE FIRST
  ├─ Budget = GlobalFlowCount
  └─ For each FSpawnEntry (in array order = implicit priority):
       ├─ Budget <= 0 → break
       ├─ Entry.GetAndPruneLiveCount()           [weak pointer safety net]
       ├─ Entry.GetVacancy() <= 0 → continue
       ├─ SpawnRequirements != null
       │    └─ Evaluate(FRequirementContext{World, Anchor, PlayerState=null})
       │         └─ !bPassed → continue
       ├─ ToSpawn = Min(Entry.GetVacancy(), Entry.GetEffectiveBudget(Budget))
       └─ For i in [0, ToSpawn):
            └─ TrySpawnForEntry(Entry)
                 ├─ EntityClass.Get() == null → return nullptr (async load pending)
                 ├─ SpawnPointConfig.ResolveSpawnTransform → false → return nullptr
                 ├─ SpawnActor<AActor>(Class, Transform, bDeferConstruction=true)
                 │    └─ null → return nullptr (collision block)
                 ├─ Actor.OnDestroyed.AddDynamic(OnSpawnedActorDestroyed)
                 ├─ Actor.FinishSpawning(Transform)
                 ├─ ISpawnableEntity::Execute_OnSpawnedByManager(Actor, Anchor)
                 ├─ Entry.LiveInstances.Add(WeakPtr(Actor))
                 └─ Budget--  (only on success)

OnSpawnedActorDestroyed(DestroyedActor)  [bound per-instance]
  └─ Scan all entries → LiveInstances.RemoveAll(matches actor or is invalid)

ComputeNextInterval(NearbyPlayers)
  ├─ Interval = BaseFlowInterval
  ├─ bScaleByNearbyPlayers && NearbyPlayers > 0
  │    └─ alpha = Clamp(NearbyPlayers / PlayerCountForMinInterval, 0, 1)
  │         Interval = Lerp(BaseFlowInterval, MinFlowInterval, alpha)
  ├─ Interval = Max(Interval, 10.0)   ← hard floor
  └─ Interval += RandRange(0.0, 1.0)  ← jitter

GetNearbyPlayerCount()
  ├─ OnCountNearbyPlayers bound → call delegate
  └─ not bound → log one-time Warning, return 0
```

---

## Known Issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| KI-1 | `GetLootTableOverrideForClass` matches on `EntityClass.Get()` which is null if the class hasn't loaded yet. Unlikely to matter since it's called from `OnSpawnedByManager` post-load, but technically fragile if called externally. | Low | See Code Review for recommended fix. |
| KI-2 | `USpawnPointConfig_PointList::ResolveSpawnTransform` contains a dead code block — the first `FindChildComponentsByTag` call resets `Candidates` before a manual rebuild loop does the same work correctly. | Low | Dead code; does not cause a bug but must be cleaned up in implementation. See Code Review. |
| KI-3 | `OnCountNearbyPlayers` is a raw `TFunction` with no lifetime guard. If the lambda captures objects (e.g. a `UGameMode*`) that get garbage collected, the call will crash. | Medium | Document that callers must ensure captured objects outlive the component, or use a `TWeakObjectPtr` inside the lambda. |
| KI-4 | `OnSpawnedActorDestroyed` scans all entries linearly. For very large `SpawnEntries` arrays this is O(N×M). | Low | Acceptable for typical use (< 20 entries). An actor-to-entry map could be added if profiling reveals this as a hotspot. |
| KI-5 | `bDelegateWarningLogged` is mutated inside a `const` method via `const_cast`. | Low | Should be `mutable bool` instead. See Code Review. |

---

## File Structure

```
GameCore/
  Source/
    GameCore/
      Spawning/
        ISpawnableEntity.h
        SpawnPointConfig.h
        SpawnPointConfig.cpp
        SpawnEntry.h
        SpawnManagerComponent.h
        SpawnManagerComponent.cpp

GameCoreEditor/
  Source/
    GameCoreEditor/
      Spawning/
        FSpawnEntryCustomization.h
        FSpawnEntryCustomization.cpp
```

### Build.cs

`GameCore.Build.cs` must include:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Engine",
    "NavigationSystem",   // UNavigationSystemV1 — RadiusRandom navmesh projection
    "GameplayTags",
    "Requirements",       // URequirementList, FRequirementContext
});
```

`GameCoreEditor.Build.cs` must include:

```csharp
PrivateDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "PropertyEditor",
    "UnrealEd",
    "SlateCore",
    "Slate",
});
```
