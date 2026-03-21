# Quest System — Code Review

This document records design concerns, architectural issues, and suggestions identified during the migration from `GameCore Specifications` to `GameCore Specifications 2`.

---

## Critical Issues

### CR-1 — `FRequirementContext` Mismatch (Fixed in Migration)

**Original spec:** used typed fields `PlayerState`, `World`, `QuestComponent`, and `PersistedData` directly on `FRequirementContext`.

**Problem:** The v2 Requirement System uses `FInstancedStruct Data` exclusively. Adding typed fields to `FRequirementContext` violates the zero-dependency contract of the `Requirements/` module and was explicitly removed in the Requirement System redesign. The old spec was internally contradictory — `GameCore Changes.md` referenced `FRequirementPayload` and `PersistedData` while the v2 Requirement System spec had already removed them.

**Fix applied:** Introduced `FQuestEvaluationContext` — a snapshot struct injected into `FRequirementContext::Data` via `FRequirementContext::Make<FQuestEvaluationContext>()`. Quest requirements cast `Context.Data.GetPtr<FQuestEvaluationContext>()` and retrieve `UQuestComponent` from `PlayerState`. Payload injection and `URequirement_Persisted` stubs removed.

---

### CR-2 — Client Completion Watcher Not Re-Registered for Late-Joined Quests (KI-3, Fixed in Migration)

**Original spec:** `RegisterClientValidatedCompletionWatchers` was called once in `BeginPlay`. Quests accepted after `BeginPlay` (the normal gameplay case) never had their client completion watchers registered.

**Fix applied:** Moved to `RegisterClientValidatedCompletionWatcher(const FQuestRuntime&)` — a single-quest version called both from the `BeginPlay` sweep and from `FQuestRuntime::PostReplicatedAdd`. This ensures every quest accepted post-login registers its client watcher on replication arrival.

---

## Significant Design Concerns

### CR-3 — `IterateAllDefinitions` Only Covers Resident Definitions (KI-1, Partially Mitigated)

**Problem:** `RegisterUnlockWatchers` calls `Registry->IterateAllDefinitions` which only iterates definitions already in memory. Quests whose assets have never been loaded are silently skipped. For a player's first login with zero prior quest activity, this means no unlock watchers are registered until some other code path loads a definition.

**Immediate mitigation:** The `UQuestRegistrySubsystem` now pre-builds `AllQuestAssetIds` at `Initialize` time. A full fix would eagerly load all definitions at login (acceptable for games with < a few hundred quests) or iterate `AllQuestAssetIds` and issue async loads, then register watchers in the callback. For a pirate MMORPG this is the recommended approach — quest counts are bounded and login is a natural loading gate.

**Recommendation:** In `RegisterUnlockWatchers`, iterate `AllQuestAssetIds` rather than resident definitions. For each unloaded quest, call `GetOrLoadDefinitionAsync` and register the watcher in the callback. The baseline evaluation fires in the same callback.

---

### CR-4 — `USharedQuestCoordinator` Has No Guard Against Coordinator GC While Members Are Active (KI-2)

**Problem:** If the group actor is destroyed (e.g. ship sinks, party disbanded) while a shared quest is in-progress, `USharedQuestComponent::GetCoordinator()` returns `nullptr`. The override falls back to base solo `Server_IncrementTracker`, which is silently correct but means the shared counter state in `USharedQuestCoordinator` is abandoned. Remaining members' trackers diverge.

**Recommendation:**
1. `USharedQuestCoordinator::Deinitialize` should call `RemoveMember` for all active members of all active shared quests before destruction. This applies the de-scaled snapshot to each member so they continue solo with carried-over progress.
2. `USharedQuestComponent` should hold a `TWeakObjectPtr<USharedQuestCoordinator>` cached at `EnrollMember` time, checked in `Server_IncrementTracker` before delegating.

---

### CR-5 — `ResolveQuestPath` Was O(n) in Original Spec

**Original:** Looped all `AllQuestAssetIds` on every path resolution.

**Fix applied:** `UQuestRegistrySubsystem` now pre-builds `QuestAssetIdByLeafName : TMap<FName, FPrimaryAssetId>` at `Initialize`. Resolution is O(1). This matters at login where many definitions may be loaded in parallel.

---

### CR-6 — `FQuestProgressTrackerDef::GetEffectiveTarget` Uses `FMath::RoundToInt` — Rounding Direction Not Specified

**Problem:** `RoundToInt` uses banker's rounding (round-half-to-even). For scaling formulas this can produce unexpected results at exactly `.5` increments. Designers may be surprised that a `ScalingMultiplier = 0.5`, `TargetValue = 5`, `GroupSize = 3` yields `FMath::RoundToInt(5 + 2 * 5 * 0.5) = 10` — which is fine — but edge cases with odd `TargetValue` will round differently than expected.

**Recommendation:** Use `FMath::CeilToInt` so that any fractional group contribution is always rounded up, favouring players. Document this in the field comment.

---

### CR-7 — No Mechanism to Propagate `LastDailyResetTimestamp` to Clients

**Problem:** `UQuestRegistrySubsystem` stores `LastDailyResetTimestamp` and `LastWeeklyResetTimestamp` on both server and client, but the spec does not define how the client receives these values. The spec says "populated at login via replication or initial RPC from the server" without specifying the mechanism.

**Recommendation:** Add a `ClientRPC` on `UQuestComponent` (or a dedicated `AGameState` replicated field) that the server calls in `OnDailyReset` / `OnWeeklyReset` to push updated timestamps to all connected clients. Alternatively, expose these as replicated fields on `AGameState` — that is the canonical Unreal pattern for server-wide state that clients need to read.

---

### CR-8 — `SingleAttempt` Abandon Behaviour Is Undefined

**Problem:** `ServerRPC_AbandonQuest` is declared but the spec does not define whether abandoning a `SingleAttempt` quest adds `QuestCompletedTag`. This is a content-design decision that must be specified.

**Recommendation:** For `SingleAttempt`, abandoning should behave like a fail — `QuestCompletedTag` is added and the quest is permanently closed. If designers want an abandon-without-closing option, the `RetryUntilComplete` lifecycle is the correct choice. Document this clearly on `EQuestLifecycle::SingleAttempt`.

---

### CR-9 — De-Scale Formula Has an Edge Case with `ScalingMultiplier = 0` and Large `CurrentShared`

**Problem:** When `ScalingMultiplier <= 0`, the formula copies `CurrentShared` directly to `SnapshotValue` without capping at `SoloTarget`. If a group achieved a shared counter far above `SoloTarget` (e.g. through overflow or an earlier bug), the leaving member would receive a counter above their solo cap.

**Recommendation:** Always apply `Min(SnapshotValue, SoloTarget)` regardless of `ScalingMultiplier`:
```cpp
SnapshotValue = FMath::Min(CurrentShared, SoloTarget);
```
This is correct: a non-scalable tracker's meaning is "the group collectively needs SoloTarget" — a solo snapshot of more than SoloTarget is always clamped.

---

## Minor Issues

### CR-10 — `UQuestComponent::RegisterUnlockWatcherForQuest` Is Referenced But Not Declared

`Internal_CompleteQuest` and `Internal_FailQuest` call `RegisterUnlockWatcherForQuest(QuestId, Def)` for non-permanent lifecycles, but this helper is not declared in the class declaration. It should be a private helper that wraps the `RegisterWatch` + baseline evaluate pattern from `RegisterUnlockWatchers`, extracted to avoid duplication.

---

### CR-11 — `FQuestRuntime::PostReplicatedAdd` Should Trigger Client Watcher Registration

As noted under CR-2, `PostReplicatedAdd` on the client is the correct hook for registering `ClientValidated` completion watchers for quests accepted after `BeginPlay`. This is declared as a requirement in the spec but the implementation link to `RegisterClientValidatedCompletionWatcher` must be explicit in the runtime struct spec to avoid being overlooked during implementation.

---

### CR-12 — No Watcher Cleanup for `ClientValidated` Completion Watchers on Client

Client-side completion watchers registered for active quests have no explicit handle storage. The spec notes they are "unregistered implicitly when the owning list asset is unloaded." For precision — especially if a quest stage advances server-side and the stale client-watcher fires for an old stage — explicit handle storage and cleanup in `PreReplicatedRemove` / `PostReplicatedChange` is safer.

**Recommendation:** Store `TMap<FGameplayTag, FEventWatchHandle> ClientCompletionWatcherHandles` on `UQuestComponent`. Unregister in `PostReplicatedChange` (stage changed → old watcher obsolete) and `PreReplicatedRemove` (quest removed → watcher no longer relevant). Re-register in `PostReplicatedAdd` and `PostReplicatedChange` (new stage).

---

### CR-13 — `GameCoreEvent.Quest.PartyInvite` Tag in Old File Structure vs `GroupInvite` in GMS Events

The old `File Structure.md` defined `GameCoreEvent.Quest.PartyInvite` but `GMS Events.md` defined `GameCoreEvent.Quest.GroupInvite`. These are the same event. The migration uses `GroupInvite` consistently throughout (avoids game-specific "party" terminology in a generic plugin).

---

## Summary

| ID | Severity | Status | Description |
|---|---|---|---|
| CR-1 | Critical | Fixed | `FRequirementContext` mismatch with v2 requirement system |
| CR-2 | Critical | Fixed | Client completion watcher not re-registered for post-login quests |
| CR-3 | High | Partially mitigated (KI-1) | `IterateAllDefinitions` skips unloaded definitions |
| CR-4 | High | Documented (KI-2) | Coordinator GC while members active |
| CR-5 | Medium | Fixed | O(n) path resolution replaced with O(1) map |
| CR-6 | Medium | Documented | `RoundToInt` rounding direction — recommend `CeilToInt` |
| CR-7 | Medium | Documented | No spec for propagating reset timestamps to clients |
| CR-8 | Medium | Documented | `SingleAttempt` abandon behaviour undefined |
| CR-9 | Low | Documented | De-scale formula edge case with `ScalingMultiplier = 0` |
| CR-10 | Low | Documented | `RegisterUnlockWatcherForQuest` undeclared helper |
| CR-11 | Low | Fixed (CR-2) | `PostReplicatedAdd` must trigger watcher registration |
| CR-12 | Low | Documented | No explicit client completion watcher handle storage |
| CR-13 | Low | Fixed | `PartyInvite` vs `GroupInvite` tag name inconsistency |
