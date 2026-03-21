# Code Review — HISM Proxy Actor System

**Reviewer perspective:** AAA game developer, UE5 expert.  
**Scope:** Design quality, architecture, correctness, performance, missing features.

---

## Overall Assessment

The system is well-conceived. The core idea — pool + spatial grid + HISM custom data hide flag — is the correct approach for this problem at MMO scale. The implementation spec is detailed and most decisions are explicitly justified. Several issues are worth addressing before implementation.

---

## Issues

### 1. `DeactivationRadiusBonus` is defined but never used in `TickProximityCheck`

**Severity: High**

`UHISMProxyConfig` defines `DeactivationRadiusBonus` to create hysteresis (separate activation and deactivation radii). However, `TickProximityCheck` uses a single `Config->ActivationRadius` for both activation and deactivation checks. The deactivation bonus is never applied.

**Fix:** When determining whether an already-active or `PendingRemoval` slot should deactivate, check against `ActivationRadius + DeactivationRadiusBonus`, not `ActivationRadius`:

```cpp
const float ActRadiusSq  = FMath::Square(Config->ActivationRadius);
const float DeactRadiusSq = FMath::Square(
    Config->ActivationRadius + Config->DeactivationRadiusBonus);

// For active/PendingRemoval instances: check against DeactRadiusSq
// For new activations: check against ActRadiusSq
```

The spatial grid query radius should use the larger value (`ActivationRadius + DeactivationRadiusBonus`) so candidates for active slots are correctly returned.

---

### 2. `TickProximityCheck` does not use `DeactivationRadiusBonus` in grid query

**Severity: Medium** (related to #1)

The `QueryRadius` call in `TickProximityCheck` uses `Config->ActivationRadius` as the query radius. For already-active instances, the effective deactivation radius is larger. Active instances near the deactivation boundary will not appear as grid candidates when they should, so their `PlayerRefCount` will incorrectly be left at 0 rather than checked.

**Fix:** Query the grid with `max(ActivationRadius, ActivationRadius + DeactivationRadiusBonus)` — i.e. always the larger radius. Candidate sets for deactivation will include all relevant instances. Then separate the distance threshold applied per-instance based on slot state.

---

### 3. Pool exhaustion leaves HISM instance without a proxy silently

**Severity: Medium**

When the pool is at `MaxPoolSize` and exhausted, activation is silently skipped. An `Error` is logged, but from a gameplay perspective the interaction is simply absent — no HISM instance is hidden, no proxy appears. The player sees an interactable-looking tree but gets no prompt. This is confusing and can be mistaken for a gameplay bug.

**Suggestions:**
- Consider hiding the HISM instance anyway (clip via CustomData) and leaving a visible marker or triggering an event, so operations teams can clearly see pool exhaustion visually during testing.
- The current approach (log + skip) is acceptable for a first implementation but should be documented as a known UX issue.

---

### 4. `TickProximityCheck` resets `TickInstancePlayerCount` but then uses `FindRef` to update `PlayerRefCount`

**Severity: Low — logic correctness**

After `TickInstancePlayerCount.Reset()`, managed instances that are active but currently have no nearby players will not appear in the map. `TickInstancePlayerCount.FindRef(InstanceIdx)` returns 0 for missing keys, which is correct. However, this means the slot's `PlayerRefCount` is updated to 0 only via `FindRef` — there is no explicit zero-out pass. This is correct **as long as `TickInstancePlayerCount` is fully rebuilt every tick**, which it is. This is fine, but worth documenting clearly to avoid confusion during maintenance.

---

### 5. No server-side `PlayerRefCount` recalculation on explicit deactivation

**Severity: Low**

`NotifyInstanceStateChanged` calls `DeactivateSlotImmediate` immediately. If a player is still in range when this fires (e.g. the harvest subsystem forces deactivation mid-session), the next `TickProximityCheck` will re-activate a proxy for that instance — but only if `OnQueryInstanceEligibility` now returns false. If the game system fails to update eligibility before the next tick, a proxy will immediately re-activate. This is a contract issue, not a system bug, but should be explicitly documented:

> **Contract:** Before calling `NotifyInstanceStateChanged`, the calling system must have already updated its internal state so that `OnQueryInstanceEligibility` returns `false` for `InstanceIndex`. Otherwise the proxy will re-activate on the next proximity tick.

---

### 6. `AllPooledActors.Empty()` in `EndPlay` may race with GC

**Severity: Low**

`EndPlay` calls `AllPooledActors.Empty()` after `DeactivateSlotImmediate`. Since all actors were already deactivated (hidden, collision off, moved to pool location), they are now unreferenced and eligible for GC. In a dedicated server context this is fine — the server shuts down and UE's GC cleans up. In PIE however, GC can run between `EndPlay` and actual world cleanup, and pooled actors (which are now hidden and not in `InstanceToSlotMap`) may be garbage collected while the outer world still holds references in edge cases.

The current approach is acceptable given the stated design decision (pool never shrinks, memory reclaimed on restart). **No change required**, but this is worth noting for PIE stability.

---

### 7. `BeginPlay` validation does not check `Entry.HISM->NumCustomDataFloats`

**Severity: Low**

`AHISMProxyHostActor::BeginPlay` validates that `HISM`, `Bridge`, `Config`, and `ProxyClass` are non-null, but does not check `NumCustomDataFloats >= 2`. If a developer manually sets `NumCustomDataFloats = 0` on the component (bypassing the editor auto-set), the hide flag and type index writes at runtime will silently no-op.

**Fix:** Add:
```cpp
if (Entry.HISM->NumCustomDataFloats < 2)
    UE_LOG(LogGameCore, Error,
        TEXT("[%s] entry %d: NumCustomDataFloats < 2 — hide flag will not work."),
        *GetName(), i);
```

---

### 8. `FHISMSpatialCell` uses `TArray<int32>` per cell

**Severity: Low — micro-optimization consideration**

Each `FHISMSpatialCell` owns a `TArray<int32>`. With 1,000+ cells, most of which contain 0–3 instances, the per-cell `TArray` header (~24 bytes) dominates. For 10,000 cells this is ~240 KB of overhead with minimal actual data.

A more cache-efficient alternative is a flat instance index array with a cell → [start, count] offset table (CSR/compact sparse row format). However, this adds implementation complexity and the current approach is correct and fast enough for the stated scale. **No change required for first implementation** — flagged as a future optimization if memory or query performance becomes a concern at very high instance counts (10k+).

---

### 9. Foliage converter does not handle World Partition sub-levels automatically

**Severity: Medium**

`AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel` returns only the persistent level's foliage actor. In a World Partition map, foliage may be distributed across multiple streaming cells. The developer guide notes this limitation but the converter has no tooling to help — it silently converts 0 instances for meshes that only exist in non-loaded cells.

**Suggestion:** Emit a prominent warning in the conversion result if the total converted count is 0 and at least one matching mesh type exists in `InstanceTypes`, to surface the World Partition issue clearly.

---

### 10. No event broadcast on proxy activation / deactivation

**Severity: Low — missing feature**

The system has no GameCore Event Bus integration at the system level. Individual proxy Blueprint subclasses can broadcast events manually, but there is no standardised event fired by the bridge on activation/deactivation.

For observing systems (analytics, debug visualisation, audio), a reliable bridge-level event would be cleaner than requiring every Blueprint to remember to broadcast.

**Suggestion:** Add optional `FSimpleMulticastDelegate OnProxyActivated` and `OnProxyDeactivated` on `UHISMProxyBridgeComponent`. These can be wired to the Event Bus in game code. Keep them as plain delegates (not GameCore Event Bus broadcasts) to preserve the system's zero-dependency-on-other-GameCore-systems principle.

---

### 11. `ValidateSetup` is editor-only but `BeginPlay` re-checks only a subset of the same conditions

**Severity: Low — code duplication**

Validation logic is split between `ValidateSetup` (comprehensive, editor-only) and `BeginPlay` (partial, runtime). In non-shipping builds, `BeginPlay` should call `ValidateSetup` or share a common internal validation function. The duplication means runtime misses the `NumCustomDataFloats` check and the pool size sanity check.

**Suggestion:** Extract shared validation into a `bool ValidateEntry(const FHISMProxyInstanceType&, FString& OutError) const` helper called by both `ValidateSetup` and `BeginPlay`.

---

## Architecture Observations

**Strengths:**
- Pool-based actor reuse is correct and the pre-allocation strategy is sound.
- Spatial grid design is appropriate for static instances at the stated scale.
- HISM custom data as the hide flag is the only correct approach given UE's API.
- Tag-based component ownership avoids fragile name-matching.
- Scratch buffer pre-allocation for the hot-path tick is correct practice.
- GC safety via `AllPooledActors UPROPERTY` is correct.
- Three-pass foliage converter correctly handles index invalidation.

**Missing but low priority:**
- Click-to-place viewport FEdMode (acknowledged as deferred in requirements).
- World Partition multi-level foliage conversion support.
- Standardised bridge-level activation/deactivation events.

**Would not change:**
- Server-only proximity logic. Client replication via standard Actor relevancy is correct.
- 2D spatial grid. Z-axis handling in the bridge's 3D distance check is sufficient.
- Pool never shrinks. The tradeoff is appropriate for dedicated server restart cycles.
- One HISM per mesh type. This correctly matches how UE's own Foliage System works.
