# Zone System — Code Review

## Overview

The Zone System is well-scoped and architecturally sound for its target scale (50–500 zones). The passive zone / opt-in tracker split is correct. GMS (via `UGameCoreEventBus`) as the broadcast mechanism is consistent with project conventions. The main issues are implementation-level bugs and a few design gaps that limit the system's robustness.

---

## Issues Found

### 1. Float Precision Bug in `PointInConvexPolygon2D` ⚠️ **Bug**

**Original code:**
```cpp
if (LastCross != 0.f && FMath::Sign(Cross) != FMath::Sign(LastCross))
    return false;
if (Cross != 0.f) LastCross = Cross;
```

Comparing a `float` to `0.f` with `!=` is unreliable. For points very close to an edge (near-boundary cases), floating-point arithmetic can produce a cross product that is a tiny non-zero value with inconsistent sign. This causes valid interior points to be incorrectly rejected.

**Fix:** Use an epsilon guard (`KINDA_SMALL_NUMBER`) before updating `LastCross` and before sign comparison. Applied in the updated spec.

---

### 2. Stale World Polygon After `InitializeZone` ⚠️ **Bug**

`UZoneConvexPolygonShapeComponent::RebuildWorldPolygon()` is called in `OnRegister` only. `AZoneActor::InitializeZone` modifies `LocalPolygonPoints` after `OnRegister` has already run (runtime spawn), leaving `WorldPolygon` stale.

The actor is subsequently registered with `UZoneSubsystem` in `BeginPlay` with an invalid cache, causing all `ContainsPoint` queries to use stale or empty data.

**Fix:** `InitializeZone` must explicitly call `ConvexShape->RebuildWorldPolygon()` and `BoxShape->RebuildCache()` after modifying shape data. Applied in the updated spec. `RebuildCache` on the box component must also be made public.

---

### 3. Missing Server-Side Broadcast on State Mutation ⚠️ **Design Flaw**

The original spec only fires `FZoneStateChangedMessage` in `OnRep_DynamicState`, which only fires on clients. Server-side listeners (authoritative gameplay systems that react to ownership changes) would never receive the event.

**Fix:** Extract a private `BroadcastStateChanged()` helper called from all mutation methods. Server broadcasts with `ServerOnly` scope; `OnRep_DynamicState` broadcasts with `ClientOnly`. Applied in the updated spec.

---

### 4. Orphan Reference: `UGameCoreEventBus2` ⚠️ **Wrong Class Name**

The original Zone Tracker Component spec references `UGameCoreEventBus2::Get(this)`. This class does not exist. The correct class is `UGameCoreEventBus` as defined in the Event Bus System specs.

**Fix:** All references corrected to `UGameCoreEventBus` throughout.

---

### 5. `GetWorldBounds` on Box Component ⚠️ **Correctness**

The original implementation used `FBox::BuildAABB(GetComponentLocation(), HalfExtent)` which assumes the box is axis-aligned in world space. If the actor or component has a non-identity rotation, the AABB is incorrect, causing mis-registration in the grid (zones that appear larger or smaller than they are in the grid index).

**Fix:** Use `GetComponentTransform().TransformVector(HalfExtent).GetAbs()` to correctly account for rotation. Applied in the updated spec.

---

### 6. No `BeginPlay` Guard on `RegisterZone` ⚠️ **Minor Issue**

`RegisterZone` silently returns if `Zone->DataAsset` is null. This is correct but silent — a placed zone actor with no data asset assigned is never registered, and the designer gets no feedback.

**Suggestion:** Add a `UE_LOG(LogGameCore, Warning, ...)` or `ensureMsgf` in non-shipping builds when `DataAsset` is null at registration time. Not critical but aids authoring.

---

### 7. Tick Group Default ⚠️ **Performance / Precision**

`UZoneTrackerComponent` uses the default tick group (`TG_DuringPhysics`). For character pawns whose `CharacterMovement` ticks in `TG_PrePhysics`, the tracker always reads the previous frame's position. For most zones this is a non-issue, but for small high-precision zones it can cause missed transitions.

**Suggestion:** Document and expose tick group configuration. Consider defaulting to `TG_PostPhysics` in the constructor for more accurate current-frame queries.

---

### 8. Both-Component Architecture on `AZoneActor` ⚠️ **Minor Code Smell**

Having both `BoxShape` and `ConvexShape` always created as components — but only one active — is simple and works, but wastes a small amount of memory and shows both components in the editor details panel, which can confuse designers.

**Suggestion (future):** A custom editor details customisation that hides the inactive shape component based on `ShapeType`. Not a blocker, but worth addressing before the system is used by a large content team.

---

### 9. `bAlwaysRelevant` Replication Scaling ⚠️ **Future Risk**

`bAlwaysRelevant = true` on all zone actors is correct for 50–500 zones. However, with frequent `FZoneDynamicState` changes (siege systems, territory captures, event zones), this could become a replication bottleneck for servers with many concurrent players.

**Suggestion:** At scale, consider using UE's `NetDormancy` system — zones with no state changes in the last N seconds can be set dormant. The zone wakes when state changes, sending the update only then. Not needed now, but document as a scaling path.

---

### 10. Non-Spatial Query Performance ⚠️ **Minor**

`GetZonesByType` and `GetZoneByName` perform full linear scans over `AllZones`. This is fine at 50–500 zones but should be documented as not suitable for per-frame use. If these are called frequently by multiple systems (minimap updates, AI queries), a secondary `TMap<FGameplayTag, TArray<...>>` type index would eliminate the scan.

**Suggestion:** Document the per-frame restriction. Add the type-indexed map if profiling shows it as a hotspot.

---

## Summary

| # | Issue | Severity | Status |
|---|---|---|---|
| 1 | Float precision in `PointInConvexPolygon2D` | Bug | Fixed in spec |
| 2 | Stale world polygon after `InitializeZone` | Bug | Fixed in spec |
| 3 | Missing server-side state change broadcast | Design flaw | Fixed in spec |
| 4 | `UGameCoreEventBus2` orphan reference | Wrong class | Fixed in spec |
| 5 | Incorrect AABB on rotated box component | Correctness | Fixed in spec |
| 6 | Silent null DataAsset at registration | Minor | Suggestion noted |
| 7 | Tick group default | Performance/Precision | Suggestion noted |
| 8 | Both shape components always visible | Code smell | Suggestion noted |
| 9 | `bAlwaysRelevant` at large scale | Future risk | Documented |
| 10 | Non-spatial query full scan | Minor | Documented |
