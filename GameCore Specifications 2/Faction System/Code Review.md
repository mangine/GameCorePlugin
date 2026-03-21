# Faction System — Code Review

**Reviewer:** GameCore Architecture Review

---

## Overview

The Faction System is well-structured. The core O(1) lookup design is sound, the sorted-pair key approach is clever and allocation-free, and the primary/secondary split cleanly solves the false-hostility problem for grouping tags. The use of the Requirement System for join gates and the Event Bus for notifications is correct and consistent with the rest of GameCore.

The following issues range from minor logic bugs to medium-severity design concerns.

---

## Issues

### 1. `GetActorRelationship` Fallback Logic Was Convoluted (Fixed)

**Severity:** Low | **Status:** Fixed in migration

**Original code:**
```cpp
return FMath::Min(...) > (uint8)EFactionRelationship::Hostile
    ? (EFactionRelationship)FMath::Min(...)
    : EFactionRelationship::Neutral;
```

This expression double-computed the min and added a confusing `> Hostile` guard that silently forced `Hostile` to `Neutral`, which is **incorrect behavior** — two actors with `FallbackRelationship::Hostile` should resolve to `Hostile`, not `Neutral`.

**Fix:** Simplified to `FMath::Min(A, B)` with a direct cast — no guard expression. The guard was masking a design confusion about whether components could ever legitimately return `Hostile` via fallback. They can.

---

### 2. `GetHostileFactions` Iterates All Registered Factions Per Call

**Severity:** Medium

`GetHostileFactions` loops over every entry in `DefinitionMap` for each call. In a project with 20+ factions and many AI actors calling this at spawn, this adds up.

**Suggested improvement:** Build a reverse lookup map at `BuildCache` time:
```
HostileMap: FactionTag → TArray<FGameplayTag> (factions hostile to it)
```
This reduces `GetHostileFactions` to a single TMap lookup. The tradeoff is memory (small) and added `BuildCache` complexity (low).

**Current state:** Acceptable for a pirate MMORPG with under ~15 factions. Should be revisited if faction count grows significantly.

---

### 3. `LocalOverrides` Has No Runtime Size Cap

**Severity:** Low

`LocalOverrides` is documented as "expected 0–3 entries" but there is no `ensure` or log warning if it grows beyond that. Since it replicates in full, a misused `LocalOverrides` array of 50 entries would replicate 50 override structs on every relevant rep cycle.

**Suggestion:** Add a non-shipping `ensure(LocalOverrides.Num() <= 8)` in `GetLifetimeReplicatedProps` or after modification, with a warning log.

---

### 4. `URequirement_FactionCompatibility` Silently Passes When `UFactionSubsystem` Is Null

**Severity:** Low

If called before `UFactionSubsystem` initializes (rare but possible in edge-case test worlds), the requirement silently passes rather than failing closed.

**Suggestion:** Return `Fail` with a clear error message when `FS` is null:
```cpp
if (!FS)
    return FRequirementResult::Fail(
        LOCTEXT("NoFactionSubsystem", "Faction system not initialized."));
```
Fail-closed is safer for a join gate. If this requirement passing silently causes a player to join a faction they shouldn't, that's a harder bug to trace than a clear error.

---

### 5. Join Requirements Are Re-evaluated Per-Requirement in a Loop

**Severity:** Low / Design Note

`JoinFaction` iterates `JoinRequirements` manually rather than using `URequirementLibrary::EvaluateAll` (if that utility exists in the v2 spec). If the library adds short-circuit evaluation or composite support later, `JoinFaction` would need to be updated separately.

**Suggestion:** Once `URequirementLibrary` is confirmed stable, route through it for consistency. For now, the manual loop is correct.

---

### 6. `FRequirementContext::PlayerState` Population Assumes `APawn` Owner

**Severity:** Low

In `JoinFaction`, `Context.PlayerState` is populated via `Cast<APawn>(GetOwner())->GetPlayerState()`. If the `UFactionComponent` is on a non-Pawn actor (an NPC base actor, a ship, etc.) this silently leaves `PlayerState` null. That's fine as long as no `JoinRequirements` require it — but it is an implicit assumption worth noting.

**Suggestion:** No code change needed. Document in `UFactionComponent` header that `JoinRequirements` must not assume `Context.PlayerState` is non-null for non-player actors.

---

### 7. No `GetPrimaryAssetId` Override on `UFactionDefinition`

**Severity:** Low / Asset Pipeline

`UFactionDefinition` extends `UPrimaryDataAsset` but does not override `GetPrimaryAssetId()`. Without this, the Asset Manager cannot track or cook these assets by type. If the project uses Asset Manager bundles for faction data (likely for streaming), this must be added.

**Suggestion:**
```cpp
virtual FPrimaryAssetId GetPrimaryAssetId() const override
{
    return FPrimaryAssetId(FName("FactionDefinition"), GetFName());
}
```
Same applies to `UFactionRelationshipTable`.

---

### 8. `BuildCache` Calls `LoadSynchronous` on Reputation Progressions at World Start

**Severity:** Low

`BuildCache` calls `LoadSynchronous` on every `UFactionDefinition::ReputationProgression` soft pointer to read `ProgressionTag`. This is a dependency on `ULevelProgressionDefinition` being loadable at world start, which adds a fragile coupling between two otherwise independent systems.

**Suggestion:** Have `UFactionDefinition` expose a `ReputationProgressionTag: FGameplayTag` field alongside the soft pointer. The `BuildCache` can read the tag directly without loading the asset. The soft pointer remains for editor reference browsing and game module use.

---

### 9. No Batch Join API

**Severity:** Low / QoL

Restoring a player's faction state at login requires calling `JoinFaction` + `SetRank` per faction. Each `JoinFaction` broadcasts a GMS event, causing listeners (AI, UI) to fire for every saved faction during login — potentially N events for N saved factions.

**Suggestion:** Add a `RestoreMemberships(TArray<FFactionMembership> Snapshot)` server-only method that sets the `Memberships` array directly (bypassing `JoinRequirements`) and fires a single `MembershipsRestored` event. Useful for save-load and admin tools. This is game-module territory but GameCore could ship the hook.

---

## Positive Notes

- The sorted-pair `TMap` key is the correct solution for symmetric relationships. Alternatives (storing both `(A,B)` and `(B,A)`) would double memory and introduce consistency risk.
- Short-circuit on `Hostile` in `GetActorRelationship` is a good micro-optimization for a hot query path.
- `FFastArraySerializer` for membership replication is the correct choice — avoids full-array replication on every membership change.
- `GetRelationshipTo` delegating fully to `UFactionSubsystem` keeps component logic clean and ensures a single code path for all resolution.
- Non-shipping validation at `BeginPlay` for unregistered primary tags is exactly the right place and severity for authoring errors.
- `LocalOverrides` being checked before the cache is the correct priority order — per-entity exceptions should always win over global defaults.
