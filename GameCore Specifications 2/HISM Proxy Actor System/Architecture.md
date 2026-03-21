# HISM Proxy Actor System — Architecture

**Module:** `GameCore` (runtime) · `GameCoreEditor` (editor-only)  
**Status:** Active Specification | **UE Version:** 5.7  
**Server-only logic. Clients receive state via standard Actor replication.**

---

## Problem Statement

An MMORPG world contains thousands of world props — trees, rocks, barrels, fishing spots, ore nodes — that must render cheaply at high density **and** support individual gameplay interactions when players are nearby.

`UHierarchicalInstancedStaticMeshComponent` (HISM) solves the rendering side: thousands of identical meshes in one draw call at near-zero CPU cost. The problem is that HISM instances are **not Actors** — they have no identity, cannot host components, and are invisible to every GameCore gameplay system.

The alternative (individual Actors for everything) is CPU-prohibitive at MMO scale on a dedicated server and destroys all GPU batching.

This system solves that by keeping a **pre-allocated pool of proxy Actors** that are activated for HISM instances when players are nearby and returned to the pool when they are not. To every other GameCore system, a proxy is indistinguishable from a hand-placed world Actor.

---

## Dependencies

### Unreal Engine Modules
| Module | Usage |
|---|---|
| `Engine` | `UHierarchicalInstancedStaticMeshComponent`, `AActor`, `UActorComponent`, `UDataAsset`, `AInstancedFoliageActor` |
| `UnrealEd` *(editor-only)* | `FPropertyEditorModule`, `IDetailCustomization`, `FScopedTransaction` |
| `PropertyEditor` *(editor-only)* | Detail panel customization |
| `Foliage` *(editor-only)* | `AInstancedFoliageActor`, `UFoliageType_InstancedStaticMesh`, `FFoliageInfo` |
| `Slate` / `SlateCore` *(editor-only)* | `SButton`, `STextBlock`, `FNotificationInfo` |

### GameCore Plugin Systems
| System | Usage |
|---|---|
| **Interaction System** | Proxy Blueprints optionally add `UInteractionComponent` — zero changes to the scanner needed |
| **Event Bus** | Optional: proxy Blueprints may broadcast events on activation/deactivation |

> The HISM Proxy system has **no hard runtime dependency** on any other GameCore system. Integration is opt-in via delegates and virtual hooks.

---

## Requirements

| ID | Requirement |
|---|---|
| R1 | Any HISM instance must be replaceable with a full Blueprint Actor when a player is nearby, and restored to HISM rendering when not |
| R2 | Seamless visual transition — proxy placed at exact instance world transform; HISM instance hidden simultaneously |
| R3 | Zero runtime spawning on the hot path — pre-allocated pool; activation = transform set + visibility toggle |
| R4 | Proxy actor base imposes no restrictions on Blueprint content |
| R5 | Server authority only — pool management, proximity, and lifecycle run on server only |
| R6 | All players checked per tick — not just a single reference player |
| R7 | Hysteresis — configurable deactivation delay; cancelled if a player re-enters during the delay window |
| R8 | Multi-mesh support on a single host actor (one `FHISMProxyInstanceType` entry per mesh type) |
| R9 | Size variation via per-instance transform scale — no extra HISM components per size |
| R10 | Full compatibility with `UInteractionComponent` — scanner requires no changes |
| R11 | Zero knowledge of game-specific systems — integration via delegate and virtual hook only |
| R12 | Spatial acceleration — O(cell-count) candidate lookup; no linear scan of all instances per tick |
| R13 | Pool exhaustion handled gracefully — warning logged, activation skipped, no crash |
| R14 | Static instances only — positions baked at level load and never move |

---

## Features

| ID | Feature |
|---|---|
| F1 | Editor host actor with auto-wiring — HISM and bridge components created automatically from `InstanceTypes` array |
| F2 | Foliage Tool integration — one-click converter imports Foliage instances and removes them from `AInstancedFoliageActor` |
| F3 | Manual instance placement via "Add Instance at Pivot" Details panel button |
| F4 | Validate Setup button — runs authoring checks and outputs to Message Log |
| F5 | Full undo support via `FScopedTransaction` for all editor mutations |
| F6 | Per-bridge eligibility delegate — game systems veto proxy activation for specific instances |
| F7 | Dynamic pool — grows from `MinPoolSize` to `MaxPoolSize` at runtime if exhausted |
| F8 | Pool sizing formula documented — designers size pools from density/radius/player-count inputs |
| F9 | GC-safe actor pooling via `UPROPERTY TArray` on the bridge component |
| F10 | Per-tick allocation-free proximity check — scratch containers are pre-allocated member arrays |

---

## Design Decisions

| ID | Decision | Rationale |
|---|---|---|
| AD-1 | One HISM component per mesh type | HISM only batches identical meshes. Mixed-mesh HISM gives no batching benefit. Matches UE's own Foliage System design |
| AD-2 | Size variation via per-instance scale | HISM supports non-uniform per-instance scaling. Proxy inherits the scale via `SetActorTransform`. No extra components |
| AD-3 | Uniform 2D spatial grid | For static instances with a fixed query radius: O(N) build, O(1) query, high cache friendliness. Octree rejected (O(N log N) build, pointer-chased queries, no benefit for this use case) |
| AD-4 | `PerInstanceCustomData[0]` as hide flag | HISM has no per-instance visibility API. GPU-side clip via custom data is O(1) write, no collision/navmesh side effects. Removing/re-adding instances rejected (rebuilds cluster tree) |
| AD-5 | Pool spawned 1km below host actor | Avoids visible location at world origin, keeps actors in the same streaming cell as the host, no navmesh/collision impact |
| AD-6 | Dynamic pool — grow, never shrink | Grow provides a safety net; shrink complexity not justified (dedicated servers restart periodically) |
| AD-7 | Tag-based component ownership (`"HISMProxyManaged"`) | Name-prefix matching is fragile — a user component named `HISM_MyCollider` would be incorrectly destroyed |
| AD-8 | `bIsRebuilding` outside `#if WITH_EDITOR` | UHT generates consistent-layout reflection code. Members inside `#if WITH_EDITOR` cause editor/non-editor layout mismatch |
| AD-9 | `RebuildTypeIndices` fast path uses `TypeIndex` field only | `GetCustomDataValue` does not exist on `UInstancedStaticMeshComponent` in UE5 — custom data is write-only to GPU |
| AD-10 | Foliage converter: three-pass + descending removal | Naive removal invalidates earlier indices. Descending-order removal avoids all index shifts. `FFoliageInstanceId` is an internal type not publicly accessible |
| AD-11 | `FoliageActor->PostEditChange()` for refresh | `FFoliageInfo::Refresh` signature varies between UE versions. `PostEditChange` is stable |
| AD-12 | Slate buttons use `CreateLambda` | `FOnClicked` is `TDelegate<FReply()>` — zero arguments. `_Raw` with trailing payload does not compile for zero-argument delegates |
| AD-13 | Pre-allocated member scratch buffers in `TickProximityCheck` | `TMap`/`TArray` local construction = heap allocation per tick. `TArray::Reset()` retains capacity, zero allocator calls after first tick |

---

## System Units

| Class | Location | Responsibility |
|---|---|---|
| `AHISMProxyHostActor` | `GameCore` runtime | Level-placed actor; owns all HISM+bridge pairs; editor entry point |
| `FHISMProxyInstanceType` | `GameCore` runtime | Per-mesh-type config: mesh, proxy class, pool sizes; auto-creates HISM and bridge |
| `UHISMProxyConfig` | `GameCore` runtime | Shared proximity/timing `UDataAsset` |
| `FHISMInstanceSpatialGrid` | `GameCore` runtime | O(1) bucketed lookup of instances near a world position |
| `UHISMProxyBridgeComponent` | `GameCore` runtime | Owns pool for one HISM component; drives proximity tick; manages slot lifecycle |
| `AHISMProxyActor` | `GameCore` runtime | Minimal `AActor` base; game-specific Blueprint subclasses add all gameplay |
| `FHISMProxyHostActorDetails` | `GameCoreEditor` | Details panel customization with Validate + Add Instance buttons |
| `UHISMFoliageConversionUtility` | `GameCoreEditor` | Editor utility to import Foliage Tool instances into the host actor |

---

## Logic Flow

### Setup (Editor)
```
Designer fills InstanceTypes[] on AHISMProxyHostActor
  └── PostEditChangeProperty fires
        ├── CreateComponentsForEntry() per new entry
        │     ├── New UHierarchicalInstancedStaticMeshComponent (NumCustomDataFloats=2)
        │     └── New UHISMProxyBridgeComponent (wired to HISM)
        │           Both tagged "HISMProxyManaged"
        ├── DestroyOrphanedComponents() removes stale managed components
        └── RebuildTypeIndices() writes PerInstanceCustomData[1] per entry
```

### Runtime — BeginPlay (Server)
```
AHISMProxyHostActor::BeginPlay
  └── Validates all entries (HISM, Bridge, Config present)

UHISMProxyBridgeComponent::BeginPlay (per bridge)
  ├── Sets PoolSpawnLocation (host XY, -100000 Z)
  ├── FHISMInstanceSpatialGrid::Build() — reads all instance transforms, O(N)
  ├── BuildPool() — spawns MinPoolSize AHISMProxyActor instances (hidden)
  └── SetTimer(TickProximityCheck, ProximityTickInterval, loop=true)
```

### Runtime — Proximity Tick (Server, every 0.5s)
```
TickProximityCheck()
  1. Gather all player pawn positions → TickPlayerPositions[]
  2. Per player: SpatialGrid.QueryRadius() → TickCandidates[]
     Per candidate: 3D distance check → TickInstancePlayerCount[InstanceIdx]++
  3. Evaluate managed slots:
     - PlayerRefCount > 0 && PendingRemoval → TickSlotsToRevive
     - PlayerRefCount == 0 && Active        → TickSlotsToDeactivate
  4. Apply deferred changes:
     - Revive: ClearTimer, State = Active
     - Deactivate: BeginDeactivation() → starts DeactivationDelay timer
  5. Activate new in-range instances not already in InstanceToSlotMap:
     - Check OnQueryInstanceEligibility delegate
     - ActivateProxyForInstance()
```

### Activation
```
ActivateProxyForInstance(InstanceIndex, WorldTransform)
  ├── Pop free slot from FreeSlotIndices (GrowPool() if empty)
  ├── SetActorTransform(WorldTransform)
  ├── SetActorHiddenInGame(false) + SetActorEnableCollision(true)
  ├── SetCustomDataValue(InstanceIndex, 0, 1.0)  ← hides HISM instance
  └── AHISMProxyActor::OnProxyActivated(InstanceIndex, WorldTransform)
        └── BoundInstanceIndex = InstanceIndex
        └── BP_OnProxyActivated() — Blueprint subclass initialises gameplay state
```

### Deactivation
```
DeactivationDelay timer fires → DeactivateSlotImmediate(SlotIdx)
  ├── AHISMProxyActor::OnProxyDeactivated()
  │     └── BP_OnProxyDeactivated() — Blueprint flushes partial state, unbinds delegates
  ├── SetActorHiddenInGame(true) + SetActorEnableCollision(false)
  ├── SetActorLocation(PoolSpawnLocation)
  ├── SetCustomDataValue(InstanceIndex, 0, 0.0)  ← restores HISM instance
  └── Push slot back to FreeSlotIndices
```

---

## Known Issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| KI-1 | No viewport FEdMode for click-to-place | Low | "Add Instance at Pivot" requires moving actor pivot manually. Future work |
| KI-2 | Pool exhaustion silently skips activation | Medium | `Error` log emitted; instance stays as HISM until next tick with a free slot |
| KI-3 | Sub-level foliage requires per-sub-level converter runs | Low | `GetInstancedFoliageActorForCurrentLevel` returns only the active level's foliage |
| KI-4 | Grid is 2D (XY only) | Low | Correct for typical terrain props. HISM spanning large Z ranges needs additional Z filtering in the bridge's validation loop |
| KI-5 | `DeactivationDelay` window may lose partial interaction state | Design | By design — `OnProxyDeactivated` is the flush point. Game systems must persist partial state there |
| KI-6 | Pool never shrinks | Low | Memory reclaimed on server restart. Mid-session shrink complexity not justified |

---

## File Structure

```
GameCore/Source/GameCore/Public/HISMProxy/
  HISMProxyHostActor.h          ← AHISMProxyHostActor + FHISMProxyInstanceType
  HISMProxyConfig.h             ← UHISMProxyConfig (header-only, no .cpp needed)
  HISMInstanceSpatialGrid.h     ← FHISMInstanceSpatialGrid + FHISMSpatialCell
  HISMProxyBridgeComponent.h    ← UHISMProxyBridgeComponent + FHISMProxySlot
  HISMProxyActor.h              ← AHISMProxyActor

GameCore/Source/GameCore/Private/HISMProxy/
  HISMProxyHostActor.cpp
  HISMInstanceSpatialGrid.cpp
  HISMProxyBridgeComponent.cpp
  HISMProxyActor.cpp

GameCore/Source/GameCoreEditor/Public/HISMProxy/
  HISMProxyHostActorDetails.h
  HISMFoliageConversionUtility.h

GameCore/Source/GameCoreEditor/Private/HISMProxy/
  HISMProxyHostActorDetails.cpp
  HISMFoliageConversionUtility.cpp
```
