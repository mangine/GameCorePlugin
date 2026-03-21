# Stats System — Code Review

---

## Overview

The Stats System is well-designed for its stated purpose: a server-authoritative, data-driven accumulator for player lifetime statistics. The architecture is clean, the decoupling is genuine, and the event-driven auto-registration pattern eliminates game-module wiring boilerplate. Below are issues, design flaws, and improvement suggestions ranging from critical to cosmetic.

---

## Issues

### 1. `FUniqueNetIdRepl` as player identity — portability risk

**Severity: Medium**

`FStatChangedEvent.PlayerId` and `FAchievementUnlockedEvent.PlayerId` use `FUniqueNetIdRepl`. This introduces a compile-time dependency on the Online Subsystem, which is not guaranteed to be present in all GameCore plugin deployments.

GameCore is a generic, reusable plugin. A project-level GUID (`FGuid`), a `TWeakObjectPtr<APlayerState>`, or a simple integer player ID would be more portable. The Online Subsystem dependency should live in the game module, not in the plugin.

**Suggestion:** Add a `FGameCorePlayerId` typedef or struct in GameCore Core that projects can configure (defaulting to `FGuid`), and use it in all event structs across the plugin.

---

### 2. `AddToStat` O(n) definition scan — not cached

**Severity: Low-Medium**

Every call to `AddToStat` linearly scans `Definitions` to find the matching `UStatDefinition`. For a player with 30–50 stat definitions and a high-frequency stat (e.g. `TotalDamageDealt` fired on every hit), this loop runs on every damage tick.

The spec acknowledges this but defers the fix. It should be implemented from the start given the system is intended for high-frequency stats in an MMORPG.

**Suggestion:** Build a `TMap<FGameplayTag, UStatDefinition*>` lookup cache in `BeginPlay`. The loop is eliminated at the cost of one `TMap` lookup per `AddToStat` call.

```cpp
// BeginPlay addition:
for (UStatDefinition* Def : Definitions)
    if (Def) DefinitionLookup.Add(Def->StatTag, Def);

// AddToStat replacement:
if (const UStatDefinition* Def = DefinitionLookup.FindRef(StatTag))
{
    if (Def->TrackingRequirements && !Def->TrackingRequirements->AreRequirementsMet(GetOwner()))
        return;
}
```

---

### 3. Content sync burden between `UStatDefinition.AffectedAchievements` and `UAchievementComponent.Definitions`

**Severity: Medium**

The system requires content authors to maintain two separate lists in sync: `UStatDefinition.AffectedAchievements` (soft refs to achievement assets) and `UAchievementComponent.Definitions` (hard refs to the same assets). A mismatch causes silent non-evaluation — the achievement simply never fires. The non-shipping validation catches the mismatch on `BeginPlay`, but only at runtime.

This is a real content-authoring hazard at scale.

**Suggestion:** Consider making `UAchievementComponent` discover its `Definitions` automatically from the loaded `UStatDefinition` assets' `AffectedAchievements` at `BeginPlay`, instead of requiring a manually maintained `Definitions` array. This eliminates the sync requirement entirely at the cost of a one-time asset scan at `BeginPlay`. The current design was chosen to keep `UAchievementComponent` in control of its own definitions — that's fine, but the sync risk should be prominently documented and the non-shipping validation should use `ensureAlwaysMsgf` (not just `UE_LOG`) to ensure developers notice.

---

### 4. `GetProgress` and `OnWatcherDirty` use `FindByPredicate` — O(n) scans

**Severity: Low**

`GetProgress` and `OnWatcherDirty` both use `Definitions.FindByPredicate` to look up an achievement by tag. These are not called on every tick, but they are called on stat-change events and watcher callbacks, which may be frequent.

**Suggestion:** Build a secondary `TMap<FGameplayTag, UAchievementDefinition*>` cache alongside `StatToAchievements` at `BeginPlay`. Eliminates `FindByPredicate` entirely.

---

### 5. `GetProgress` is misleadingly callable on client

**Severity: Low**

The spec documents `GetProgress` as callable client-side, but `UStatComponent::GetStat` returns `0` on the client (values not replicated). This means progress bars populated client-side will always show 0/n. The comment in `UAchievementComponent.md` mentions this, but the function signature and Blueprint exposure do not communicate this limitation.

**Suggestion:** Either add a comment to the `UFUNCTION` specifier (`meta=(ToolTip="Server only: stat values are not replicated")`) or make `GetProgress` `BlueprintAuthorityOnly` and document a separate client-facing RPC pattern in the Usage guide.

---

### 6. No warning when `ExtractIncrement` returns a suspiciously small non-zero value

**Severity: Cosmetic / Authoring Quality**

A rule returning a very small epsilon (e.g. floating-point rounding artefact) will silently increment a stat. In non-shipping builds, a `UE_LOG(Warning)` when `Delta > 0.f && Delta < KINDA_SMALL_NUMBER` would help catch badly authored or malfunctioning rules early.

---

### 7. Achievement system does not handle mid-session definition changes

**Severity: Informational**

The lookup map is built once at `BeginPlay`. If a live game tries to add/remove achievement definitions at runtime (e.g. seasonal content), the map is stale. The spec does not address this because it is out of scope for a spec-phase system, but it should be noted as a known limitation to avoid surprises.

---

### 8. `RequirementPayloads` persisted indefinitely if `InjectRequirementPayload` is called for a non-existent achievement

**Severity: Low**

`InjectRequirementPayload` checks `EarnedAchievements.HasTag` but does not validate that the `AchievementTag` exists in `Definitions`. A game-module bug that calls `InjectRequirementPayload` for a misspelled or removed achievement tag will persist a dead payload entry indefinitely (it is never cleaned up since `GrantAchievement` is never called for it).

**Suggestion:** Add a guard: if `AchievementTag` is not found in `Definitions`, log an error in non-shipping builds and return without storing.

---

### 9. `EarnedAchievements` replication: initial value on late-joining clients

**Severity: Low**

`EarnedAchievements` is replicated with `COND_OwnerOnly` via `DOREPLIFETIME_CONDITION`. When a client reconnects mid-session, the full container is sent on initial replication, which is correct. However, if the client connects after some achievements have already been granted, `OnRep_EarnedAchievements` fires once with the full set — not incrementally. This means any one-shot "achievement just unlocked" UI animation would fire for all pre-existing achievements on reconnect. The game module must distinguish initial-load replication from incremental updates.

**Suggestion:** Track a `PreviousEarnedCount` or a `bInitialReplicationDone` flag on the client so `OnRep_EarnedAchievements` can distinguish "reconnect full-sync" from "new unlock".

---

## Positive Aspects

- **Auto-registration from DataAssets** is excellent — eliminates an entire category of game-module wiring bugs.
- **Dirty-flag + flush persistence** is the right choice for high-frequency stats.
- **Soft refs from `UStatDefinition` to `UAchievementDefinition`** correctly avoids a hard module dependency.
- **Requirement watcher unregistered on grant** is correctly handled — no resource leaks from monotonic achievements.
- **`ExtractIncrement` const-stateless contract** is clearly specified and enforced by the type system (const method).
- **Non-shipping `ValidateDefinitions`** is present and comprehensive. The use of `checkf` (crash on bad data) rather than `UE_LOG` (silently continue) is the right call for authoring errors that would otherwise cause silent misbehaviour.
