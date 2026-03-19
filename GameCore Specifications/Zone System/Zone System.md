# Zone System

## Overview

The Zone System defines named, shaped regions of the world. Zones are passive volumes — they do not track occupants. Any actor that wants zone-awareness opts in via `UZoneTrackerComponent`. The registry (`UZoneSubsystem`) provides fast spatial queries. Zone data is replicated to clients so both server and client can independently evaluate enter/exit transitions.

---

## Requirements

- Zones are world actors placed in the editor or spawned at runtime (event zones). Once spawned they are static — no movement or reshape.
- Shape support: **Box (AABB)** and **Convex Polygon with height range** (2D footprint + min/max Z).
- No occupant tracking on the zone itself. Zones are queried, not subscribed to.
- Any actor can opt into zone tracking via `UZoneTrackerComponent`.
- Zone stacking is supported: an actor can be inside multiple zones simultaneously.
- Zones carry **static metadata** (`UZoneDataAsset`) and **mutable replicated state** (`FZoneDynamicState`): owner tag, dynamic gameplay tags.
- Zone type is expressed entirely via `FGameplayTag` — no enum. The game module defines its own tag taxonomy.
- Authority lives on the server. Zone actors replicate to clients. `UZoneTrackerComponent` runs on both sides so clients fire local cosmetic/HUD events without waiting for server confirmation.
- GMS (`UGameplayMessageSubsystem`) is the broadcast mechanism for enter/exit events — no custom delegates on the zone actor.
- Scale target: 50–500 zones per map. A uniform 2D grid index is sufficient.

---

## Key Design Decisions

| Decision | Rationale |
|---|---|
| Zones are passive (no occupant list) | Eliminates server-side tracking overhead; queries are cheap at this scale |
| `UZoneTrackerComponent` is opt-in | Decoupled; only actors that care pay the cost |
| Shape tested via virtual `ContainsPoint()` | Polymorphic; AABB and convex polygon share one interface |
| Uniform grid as spatial index | 50–500 static zones — a grid pre-filter beats a full scan, avoids quadtree complexity |
| GMS for enter/exit broadcast | Consistent with project event patterns; zero coupling between zone and listener |
| Mutable state in `FZoneDynamicState` on the actor | Replicated via normal UE property replication; clean separation from static asset data |
| Client runs its own `UZoneTrackerComponent` | Enables local cosmetic/HUD prediction without a round-trip |
| Convex polygon is 2D footprint + height band | Sufficient for island coastlines and territory borders; avoids full 3D mesh complexity |

---

## System Components

| File | Contents |
|---|---|
| [Zone Data](Zone%20Data.md) | `UZoneDataAsset`, `FZoneDynamicState`, `FZoneShapeData`, shape enums |
| [Zone Actor](Zone%20Actor.md) | `AZoneActor`, `UZoneShapeComponent` (Box + ConvexPolygon variants) |
| [Zone Subsystem](Zone%20Subsystem.md) | `UZoneSubsystem`, uniform grid index, query API |
| [Zone Tracker Component](Zone%20Tracker%20Component.md) | `UZoneTrackerComponent`, enter/exit detection, GMS broadcast |
| [GMS Messages](GMS%20Messages.md) | `FZoneTransitionMessage` payload, channel tag conventions |

---

## Quick-Start Integration Guide

### 1. Author a Zone

1. Create a `UZoneDataAsset` in the editor. Set `ZoneNameTag`, `ZoneTypeTag`, and any `StaticGameplayTags`.
2. Place an `AZoneActor` in the level. Assign the Data Asset. Choose shape (Box or ConvexPolygon) and configure its `UZoneShapeComponent`.

### 2. Make an Actor Zone-Aware

Add `UZoneTrackerComponent` to the actor (player pawn, ship, etc.):

```cpp
// In actor constructor
ZoneTracker = CreateDefaultSubobject<UZoneTrackerComponent>(TEXT("ZoneTracker"));
```

The component handles everything automatically from `BeginPlay`.

### 3. Listen for Zone Transitions

Anywhere in the game module, subscribe via GMS:

```cpp
UGameplayMessageSubsystem& GMS = UGameplayMessageSubsystem::Get(this);
GMS.RegisterListener(
    GameCore::Zone::Tags::Channel_Transition,
    this,
    &UMySystem::OnZoneTransition
);

void UMySystem::OnZoneTransition(FGameplayTag Channel, const FZoneTransitionMessage& Msg)
{
    if (Msg.bEntered)
    {
        // Msg.ZoneActor, Msg.StaticData, Msg.DynamicState, Msg.TrackedActor
    }
}
```

### 4. Point Query (without a tracker)

```cpp
UZoneSubsystem* ZoneSys = GetWorld()->GetSubsystem<UZoneSubsystem>();
TArray<AZoneActor*> Zones = ZoneSys->QueryZonesAtPoint(SomeLocation);
```

### 5. Spawn a Runtime Zone (event zone)

```cpp
// Server only
FActorSpawnParameters Params;
AZoneActor* NewZone = GetWorld()->SpawnActor<AZoneActor>(AZoneActor::StaticClass(), Transform, Params);
NewZone->InitializeZone(DataAsset, ShapeData);
// UZoneSubsystem auto-registers on BeginPlay via AZoneActor
```
