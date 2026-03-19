# HISM Proxy Actor System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The HISM Proxy Actor System bridges the gap between `UHierarchicalInstancedStaticMeshComponent` (HISM) rendering and full gameplay Actor functionality. HISM instances are not Actors — they have no individual identity, cannot host components, and are invisible to gameplay systems like the Interaction System, physics callbacks, or ability targeting.

This system solves that by maintaining a **pre-allocated pool of proxy Actors** (`AHISMProxyActor`) that are activated for HISM instances when players are nearby, and returned to the pool when they are not. From every other GameCore system's perspective, a proxy Actor is indistinguishable from any hand-placed world Actor.

---

## System Units

| Unit | Class | Responsibility |
|---|---|---|
| Host Actor | `AHISMProxyHostActor` | Level-placed actor; owns all HISM+bridge pairs; editor entry point |
| Instance Type | `FHISMProxyInstanceType` | Per-mesh-type config: mesh, proxy class, pool size, radii |
| Config Asset | `UHISMProxyConfig` | Shared proximity/timing config; referenced by each instance type |
| Spatial Grid | `FHISMInstanceSpatialGrid` | O(1) bucketed lookup of instances near a world position |
| Bridge Component | `UHISMProxyBridgeComponent` | Owns pool for one HISM component; drives proximity checks |
| Proxy Actor Base | `AHISMProxyActor` | Minimal AActor base; game-specific BP subclasses add all gameplay |
| Host Actor Details | `FHISMProxyHostActorDetails` | Editor-only Details panel customization (GameCoreEditor module) |
| Foliage Converter | `UHISMFoliageConversionUtility` | Editor utility to import Foliage Tool instances into the host actor |

---

## File Layout

```
GameCore Specifications/HISM Proxy Actor System/
  HISM Proxy Actor System.md              <- this file (overview + architecture)
  Requirements and Design Decisions.md    <- WHY: requirements, features, design rationale
  AHISMProxyHostActor.md                  <- host actor + FHISMProxyInstanceType
  UHISMProxyConfig.md                     <- shared proximity config asset
  FHISMInstanceSpatialGrid.md             <- spatial acceleration structure
  UHISMProxyBridgeComponent.md            <- pool owner and proximity driver
  AHISMProxyActor.md                      <- proxy actor base class
  Editor Tooling.md                       <- Details panel + foliage converter
  Developer Guide.md                      <- end-to-end workflow reference
```

> **Start here if you need to understand why a design decision was made:** `Requirements and Design Decisions.md` contains the original requirements (R1–R14), explicit feature requirements (F1–F10), and 13 annotated architectural decisions (AD-1 through AD-13) covering every major trade-off in the system.

Runtime source lives under:

```
GameCore/Source/GameCore/Public/HISMProxy/
  HISMProxyHostActor.h
  HISMProxyInstanceType.h
  HISMProxyConfig.h
  HISMInstanceSpatialGrid.h
  HISMProxyBridgeComponent.h
  HISMProxyActor.h

GameCore/Source/GameCore/Private/HISMProxy/
  HISMProxyHostActor.cpp
  HISMInstanceSpatialGrid.cpp
  HISMProxyBridgeComponent.cpp
  HISMProxyActor.cpp
```

Editor source lives under:

```
GameCore/Source/GameCoreEditor/Public/HISMProxy/
  HISMProxyHostActorDetails.h
  HISMFoliageConversionUtility.h

GameCore/Source/GameCoreEditor/Private/HISMProxy/
  HISMProxyHostActorDetails.cpp
  HISMFoliageConversionUtility.cpp
```

---

## Architecture Overview

```
AHISMProxyHostActor  (one per forest/prop-cluster, level-placed)
  │
  ├── FHISMProxyInstanceType[0]  (e.g. Oak)
  │     ├── UHierarchicalInstancedStaticMeshComponent  "HISM_Oak"
  │     │     StaticMesh = SM_Oak
  │     │     NumCustomDataFloats = 2   <- slot 0: hide flag, slot 1: type index
  │     │     [instance 0] transform + CustomData[0]=0, [1]=0.0
  │     │     [instance 1] transform + CustomData[0]=0, [1]=0.0
  │     │     ...
  │     └── UHISMProxyBridgeComponent  "Bridge_Oak"
  │           TargetHISM → HISM_Oak
  │           Config → DA_Forest_ProxyConfig
  │           pool: N x BP_OakProxy actors (hidden below terrain)
  │
  ├── FHISMProxyInstanceType[1]  (e.g. Pine)
  │     ├── UHierarchicalInstancedStaticMeshComponent  "HISM_Pine"
  │     └── UHISMProxyBridgeComponent  "Bridge_Pine"
  │
  └── FHISMProxyInstanceType[2]  (e.g. Birch)
        ├── UHierarchicalInstancedStaticMeshComponent  "HISM_Birch"
        └── UHISMProxyBridgeComponent  "Bridge_Birch"
```

**Size variation** is per-instance transform scale — not separate HISM components. A forest of Oak trees at four sizes uses one `HISM_Oak` component where each instance has a different scale baked into its transform (e.g. 0.6x, 0.8x, 1.0x, 1.3x). The proxy actor inherits this scale via `SetActorTransform`. No additional components required.

---

## Runtime Flow

```
Default state (no players nearby):
  500 oak instances rendered by HISM_Oak — 1 draw call
  0 proxy Actors live in the world

Player approaches instance #42:
  Bridge_Oak proximity tick fires (every 0.5s)
  Spatial grid query returns instance #42 as candidate
  PerInstanceCustomData[0] for #42 set to 1.0 → HISM hides it
  BP_OakProxy actor pulled from pool, placed at instance #42 world transform
  BP_OakProxy.OnProxyActivated(42) called
  Interaction system, harvest system etc. see a normal Actor

Player leaves:
  DeactivationDelay timer starts (e.g. 5s)
  New player enters before timer fires:
    Timer cancelled, proxy stays active
  No new player — timer fires:
    BP_OakProxy.OnProxyDeactivated() called
    Proxy hidden + collision off, returned to pool
    PerInstanceCustomData[0] for #42 set to 0.0 → HISM shows it again
    Back to 500 HISM instances, 0 proxy Actors
```

---

## Design Principles

- **Server-only logic.** Pool allocation, proximity checks, and slot management run exclusively on the server. Proxy Actors replicate to clients via UE's standard Actor relevancy — no manual per-client visibility management.
- **No runtime spawning on the hot path.** The pool is pre-allocated at `BeginPlay`. Activation is `SetActorTransform` + `SetActorHiddenInGame(false)`, never `SpawnActor`.
- **One HISM component per mesh type.** HISM can only batch identical meshes. Having separate components per type is both correct and necessary — it matches what the Foliage System does internally.
- **Size is per-instance scale, not per-component.** Size variants of the same mesh share one HISM component. The proxy actor inherits the instance's scale from its transform.
- **Type metadata lives in `PerInstanceCustomData[1]`.** The host actor writes a float type index when adding instances. No external delegate binding needed for homogeneous HISM sets.
- **Slot 0 of PerInstanceCustomData is reserved** as the hide flag. Game-specific data starts at slot 2. `NumCustomDataFloats` is set to 2 minimum by the host actor.
- **Hysteresis is per-slot.** Each slot independently tracks `PlayerRefCount`. A new player cancels any pending deactivation timer for that specific slot.
- **Static instances only.** Instance positions are baked at level load and never move. Use normal Actors for anything that repositions at runtime.

---

## Integration with the Interaction System

Proxies are standard Actors. Add `UInteractionComponent` to the proxy Blueprint exactly as any other interactable Actor. The Interaction System's scanner discovers it via overlap with no changes to the scanner.

```cpp
// When a harvest completes and the instance should go on cooldown:
Bridge->NotifyInstanceStateChanged(InstanceIndex);

// Or to disable just the interaction entry without deactivating the proxy:
Proxy->GetInteractionComponent()->SetEntryServerEnabled(0, false);
```

---

## Limitations

- **Static instances only.** Instances that reposition at runtime are not supported.
- **`PerInstanceCustomData` slot 0** is always the hide flag. Slot 1 is the type index. Game custom data starts at slot 2.
- **Pool exhaustion** logs a warning and skips activation. Raise `MinPoolSize` if this appears in production logs.
- **One `UHISMProxyBridgeComponent` per HISM component.** Each bridge manages exactly one HISM. The host actor enforces this automatically.
