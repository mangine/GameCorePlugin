# Zone System — Architecture

## Overview

The Zone System defines named, shaped regions of the world. Zones are **passive volumes** — they hold no occupant list. Any actor that wants zone-awareness opts in by adding `UZoneTrackerComponent`. The registry (`UZoneSubsystem`) provides fast spatial queries via a uniform 2D grid.

Zone data is replicated to all clients so both server and client can independently evaluate enter/exit transitions, enabling immediate local cosmetic responses without a server round-trip.

---

## Dependencies

### Unreal Engine Modules
| Module | Reason |
|---|---|
| `Engine` | `AActor`, `UActorComponent`, `USceneComponent`, `UPrimaryDataAsset` |
| `GameplayTags` | `FGameplayTag`, `FGameplayTagContainer`, tag-based zone type/name identity |
| `GameplayMessageSubsystem` | Required by `UGameCoreEventBus` (transitive) |
| `NetCore` / `CoreOnline` | Replication (`DOREPLIFETIME`, `ReplicatedUsing`) |

### GameCore Plugin Systems
| System | Usage |
|---|---|
| **Event Bus** (`UGameCoreEventBus`) | Broadcasting `FZoneTransitionMessage` and `FZoneStateChangedMessage` |

> The Zone System has **no dependency** on the Requirement System, Serialization System, State Machine, or Backend. It is one of the lowest-level systems in the plugin.

---

## Requirements

- Zones are world actors placed in the editor or spawned at runtime. Once active they are **static** — no movement or reshape during play.
- Shape support: **Box (AABB)** and **Convex Polygon with height range** (2D XY footprint + min/max Z).
- Zone actors have no occupant tracking. They are queried, not subscribed to.
- Any actor opts into zone tracking via `UZoneTrackerComponent` — non-opted actors pay zero cost.
- **Zone stacking** is supported: an actor can be inside multiple zones simultaneously.
- Zones carry **static metadata** (`UZoneDataAsset`) and **mutable replicated state** (`FZoneDynamicState`): owner tag, dynamic gameplay tags.
- Zone type is expressed entirely via `FGameplayTag`. No enums are used for zone type/name identity — the game module defines its own tag taxonomy.
- Authority lives on the server. Zone actors replicate `FZoneDynamicState` to all clients.
- `UZoneTrackerComponent` runs identically on server and client — clients fire local cosmetic/HUD events without waiting for server confirmation.
- The Event Bus (`UGameCoreEventBus`) is the sole broadcast mechanism — no delegates declared on zone actors or components.
- Scale target: **50–500 zones per map**. A uniform 2D grid index is sufficient.

---

## Features

- Box and Convex Polygon zone shapes with virtual `ContainsPoint()`
- Uniform 2D grid spatial index with configurable cell size
- Priority-sorted query results for overlap resolution
- `UZoneTrackerComponent`: tick-based enter/exit detection with configurable query interval
- `LocationAnchor` support on tracker for large actors (ships, vehicles)
- Runtime zone spawning via `InitializeZone()` on server
- Replicated `FZoneDynamicState` with owner tag and dynamic gameplay tags
- Full Event Bus integration: `FZoneTransitionMessage` and `FZoneStateChangedMessage`
- `bAlwaysRelevant = true` on zone actors — all clients receive all zones regardless of position
- Game-module subclassing of `UZoneDataAsset` for domain-specific fields

---

## Design Decisions

| Decision | Rationale |
|---|---|
| Zones are passive (no occupant list) | Eliminates server-side tracking overhead; spatial queries are cheap at 50–500 scale |
| `UZoneTrackerComponent` is opt-in | Decoupled; only actors that care pay the tick cost |
| Shape via virtual `ContainsPoint()` | Polymorphic; AABB and convex polygon share one interface, no switch statements at query sites |
| Uniform grid as spatial index | 50–500 static zones — a grid pre-filter beats a full scan, avoids quadtree complexity |
| `UGameCoreEventBus` for enter/exit broadcast | Consistent with project event patterns; zero coupling between zone and listener |
| Mutable state in `FZoneDynamicState` on the actor | Replicated via standard UE property replication; clean separation from static asset data |
| Client runs its own `UZoneTrackerComponent` | Enables immediate local cosmetic/HUD prediction without a server round-trip |
| Convex polygon is 2D footprint + height band | Sufficient for island coastlines and territory borders; avoids full 3D mesh complexity |
| Both components exist on `AZoneActor` (only active one used) | Simpler than dynamic component spawning; UPROPERTY visibility in editor |
| `bAlwaysRelevant = true` | Zones must be known to all clients on join; at 50–500 zones this is acceptable |
| Server broadcasts `FZoneStateChangedMessage` on mutation | Server-side listeners react immediately without waiting for a rep-notify round-trip |

---

## Logic Flow

### Zone Registration
```
AZoneActor::BeginPlay()
  └── UZoneSubsystem::RegisterZone(this)
        ├── AllZones.AddUnique(Zone)
        └── Grid[cell].Add(Zone)  for each cell overlapping Zone->GetWorldBounds()
```

### Zone Deregistration
```
AZoneActor::EndPlay()
  └── UZoneSubsystem::UnregisterZone(this)
        ├── AllZones.Remove(Zone)
        └── Grid[cell].Remove(Zone)  for each overlapping cell
```

### Spatial Query
```
UZoneSubsystem::QueryZonesAtPoint(WorldPoint)
  ├── Hash point → FIntPoint cell key
  ├── Look up Grid[cell] → candidate list
  ├── For each candidate: AZoneActor::ContainsPoint(WorldPoint)  [exact test]
  └── Sort by Priority descending → return
```

### Zone Enter/Exit Detection (Tracker)
```
UZoneTrackerComponent::TickComponent(DeltaTime)
  ├── Accumulate TimeSinceLastQuery
  └── if >= QueryInterval:
        EvaluateZones()
          ├── UZoneSubsystem::QueryZonesAtPoint(GetQueryLocation()) → NewZones
          ├── For each Zone in CurrentZones not in NewZones:
          │     BroadcastTransition(Zone, bEntered=false)
          ├── For each Zone in NewZones not in CurrentZones:
          │     BroadcastTransition(Zone, bEntered=true)
          └── CurrentZones = NewZones

BroadcastTransition(Zone, bEntered)
  └── UGameCoreEventBus::Broadcast<FZoneTransitionMessage>(Channel_Transition, Msg, Both)
```

### Dynamic State Change (Server)
```
AZoneActor::SetOwnerTag(NewOwner)  [Authority only]
  ├── DynamicState.OwnerTag = NewOwner
  └── UGameCoreEventBus::Broadcast<FZoneStateChangedMessage>(Channel_StateChanged, Msg, ServerOnly)
      → Replication sends FZoneDynamicState to clients
          → AZoneActor::OnRep_DynamicState()
              └── UGameCoreEventBus::Broadcast<FZoneStateChangedMessage>(Channel_StateChanged, Msg, ClientOnly)
```

---

## Known Issues

1. **Float precision in `PointInConvexPolygon2D`**: The cross-product sign check uses `Cross != 0.f` and `FMath::Sign` comparison. Near-boundary or collinear points can produce an incorrect result due to exact float comparison. Should use a small epsilon (`KINDA_SMALL_NUMBER`) instead of comparing to zero.

2. **Convex polygon assumes static transforms**: `RebuildWorldPolygon()` is called once in `OnRegister`. If the zone actor's transform is modified after registration (e.g. during `InitializeZone`), the cached world polygon becomes stale. `InitializeZone` must call `OnRegister` or `RebuildWorldPolygon` after modifying shape data.

3. **`bAlwaysRelevant` at large scale**: At 500+ zones with frequent dynamic state changes, `bAlwaysRelevant = true` may generate significant replication traffic. Interest management should be revisited if zone count grows beyond 500.

4. **Tracker initial state**: On `BeginPlay`, `CurrentZones` is empty so the first evaluation fires `bEntered = true` for all zones the actor starts inside. This is intentional and correct — but listeners must be registered before `BeginPlay` completes or they miss the initial event.

5. **No tick-group ordering guarantee**: `UZoneTrackerComponent` ticks in `TG_DuringPhysics` by default. If movement is applied in `TG_PrePhysics`, position is always one tick behind. Consider setting tick group to `TG_PostPhysics` for accurate current-frame positions.

---

## File Structure

```
GameCore/Source/GameCore/
  Zone/
    ZoneTypes.h                       — EZoneShapeType, FZoneShapeData, FZoneDynamicState
    ZoneDataAsset.h / .cpp            — UZoneDataAsset
    ZoneShapeComponent.h / .cpp       — UZoneShapeComponent (abstract), UZoneBoxShapeComponent,
                                        UZoneConvexPolygonShapeComponent
    ZoneActor.h / .cpp                — AZoneActor
    ZoneSubsystem.h / .cpp            — UZoneSubsystem (UWorldSubsystem)
    ZoneTrackerComponent.h / .cpp     — UZoneTrackerComponent
    ZoneMessages.h                    — FZoneTransitionMessage, FZoneStateChangedMessage
    ZoneChannelTags.h / .cpp          — GameCore::Zone::Tags namespace constants
```
