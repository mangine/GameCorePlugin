# HISM Proxy Actor System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The HISM Proxy Actor System bridges the gap between `UHierarchicalInstancedStaticMeshComponent` (HISM) rendering and full gameplay Actor functionality. HISM instances are not Actors — they have no individual identity, cannot host components, and are invisible to systems like the Interaction System, physics callbacks, or ability targeting. This system solves that by maintaining a **pre-allocated pool of proxy Actors** (`AHISMProxyActor`) that are activated for HISM instances when players are nearby, and returned to the pool when they are not.

From every other GameCore system's perspective, a proxy Actor is indistinguishable from any handplaced world Actor. No scanner changes. No validation changes. No new interfaces.

---

## System Units

| Unit | Class | Responsibility |
|---|---|---|
| Config Asset | `UHISMProxyConfig` | Designer-authored radii, delay, pool size, tag→class map |
| Spatial Grid | `FHISMInstanceSpatialGrid` | O(1) bucketed lookup of instances near a world position |
| Bridge Component | `UHISMProxyBridgeComponent` | Owns pool, drives proximity checks, manages slot lifecycle |
| Proxy Actor Base | `AHISMProxyActor` | Lightweight AActor base; game-specific BP subclasses do the rest |

---

## File Layout

```
GameCore Specifications/HISM Proxy Actor System/
  HISM Proxy Actor System.md          <- this file (overview + integration)
  UHISMProxyConfig.md                 <- config data asset
  FHISMInstanceSpatialGrid.md         <- spatial acceleration structure
  UHISMProxyBridgeComponent.md        <- pool owner and proximity driver
  AHISMProxyActor.md                  <- proxy actor base class
```

Source lives under:

```
GameCore/Source/GameCore/Public/HISMProxy/
  HISMProxyConfig.h
  HISMInstanceSpatialGrid.h
  HISMProxyBridgeComponent.h
  HISMProxyActor.h

GameCore/Source/GameCore/Private/HISMProxy/
  HISMInstanceSpatialGrid.cpp
  HISMProxyBridgeComponent.cpp
  HISMProxyActor.cpp
```

---

## How the Pieces Connect

```
AMyHISMHostActor (level-placed)
  └── UHierarchicalInstancedStaticMeshComponent  (renders N tree/rock/barrel instances)
  └── UHISMProxyBridgeComponent
        │  reads UHISMProxyConfig
        │  builds FHISMInstanceSpatialGrid at BeginPlay (server only)
        │  runs proximity tick every 0.5s (server only)
        │
        ├── On instance enters range:
        │     acquire AHISMProxyActor from pool by type tag
        │     set transform to instance world transform
        │     hide HISM instance via PerInstanceCustomData
        │     call OnProxyActivated(InstanceIndex)
        │
        └── On all players leave range (after DeactivationDelay):
              call OnProxyDeactivated()
              hide proxy Actor (SetActorHiddenInGame + collision off)
              restore HISM instance visibility
              return slot to free list
```

---

## Design Principles

- **Server-only logic.** Pool allocation, proximity checks, and slot management run exclusively on the server. Proxy Actors replicate to clients via UE's standard Actor relevancy — no manual per-client visibility management.
- **No runtime spawning on the hot path.** The pool is pre-allocated at `BeginPlay`. Activation is a `SetActorTransform` + `SetActorHiddenInGame(false)` call, not a `SpawnActor` call.
- **Delegate-driven eligibility.** The bridge component has zero knowledge of game-specific instance state. External systems bind `OnQueryInstanceEligibility` and `OnQueryInstanceType` to tell it which instances are eligible and what proxy class to use.
- **Hysteresis is per-slot.** Each slot tracks `PlayerRefCount` independently. A new player entering range cancels any pending deactivation timer for that slot.
- **HISM instance hiding via `PerInstanceCustomData`.** When a proxy is live, the HISM instance is hidden by writing a hide flag into `PerInstanceCustomData[0]`. The HISM material reads this value and discards the instance in the pixel shader. This avoids removing/re-adding instance transforms.
- **One bridge per HISM Actor.** Do not add multiple `UHISMProxyBridgeComponent` instances to the same Actor. One component manages one `UHierarchicalInstancedStaticMeshComponent`.
- **Static instances only.** HISM instances managed by this system must be static (positions baked at level load). Dynamic HISM (procedurally repositioned instances) is out of scope — use normal Actors for those.

---

## Quick Setup

### 1. Prepare the HISM Material

The material must read `PerInstanceCustomData[0]` and clip the pixel when it equals `1.0`:

```
// In Material Graph (pseudo-HLSL)
float hideFlag = GetPerInstanceCustomData(0, 0.0f);
clip(hideFlag > 0.5f ? -1 : 1);
```

This is a one-time material setup shared across all HISM actors using this system.

### 2. Create a Proxy Config Asset

In the editor: right-click in Content Browser → **GameCore → HISM Proxy Config**.

Fill in:
- `ActivationRadius` — distance at which instances get a proxy
- `DeactivationRadiusBonus` — hysteresis band added on top
- `DeactivationDelay` — seconds to wait after all players leave before deactivating
- `PoolSize` — max concurrent live proxies
- `ProxyClasses` — array of `{ Tag, Class }` pairs

### 3. Set Up the HISM Host Actor

```
AMyForestActor
  └── UHierarchicalInstancedStaticMeshComponent  (StaticMesh = OakTree, instances added)
  └── UHISMProxyBridgeComponent
        Config = DA_OakTree_ProxyConfig
```

In `BeginPlay` of the host actor (or a game-specific manager):

```cpp
UHISMProxyBridgeComponent* Bridge = GetComponentByClass<UHISMProxyBridgeComponent>();

Bridge->OnQueryInstanceEligibility.BindUObject(
    HarvestSystem, &UHarvestSystem::IsTreeEligible);

Bridge->OnQueryInstanceType.BindUObject(
    HarvestSystem, &UHarvestSystem::GetTreeProxyType);
```

### 4. Create a Proxy Actor Blueprint

Create a Blueprint subclass of `AHISMProxyActor`. Add:
- `UInteractionComponent` with harvest entry
- `UStaticMeshComponent` (optional — only if you need a different LOD mesh on the proxy)
- Override `BP_OnProxyActivated` to initialize state from the owning game system
- Override `BP_OnProxyDeactivated` to clean up

Register the BP class in the config asset under the appropriate `ProxyTypeTag`.

---

## Integration with the Interaction System

Proxies are standard Actors. Add `UInteractionComponent` to the proxy Blueprint exactly as you would any other interactable Actor. The Interaction System's scanner will discover it via overlap without any changes.

When the owning game system changes the instance's interactability (e.g. a tree was harvested and is on cooldown), call:

```cpp
// On the proxy Actor's InteractionComponent:
InteractionComp->SetEntryServerEnabled(0, false);

// Or notify the bridge to force-deactivate the proxy entirely:
Bridge->NotifyInstanceStateChanged(InstanceIndex);
```

---

## Limitations

- **Static instances only.** Dynamic HISM repositioning is not supported.
- **One HISM component per bridge.** Multiple mesh types need multiple host Actors or future extension.
- **Pool exhaustion silently skips activation.** If all pool slots are occupied, the instance does not get a proxy. Log warnings are emitted. Size the pool to your expected concurrent density.
- **PerInstanceCustomData slot 0 is reserved.** Game-specific custom data must use slots 1+.
