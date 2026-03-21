# Progression System — Code Review

## Overview

The Progression System is architecturally sound: tag-keyed, data-driven, server-authoritative, with clean separation between the policy layer (subsystem) and state owners (components). FastArray replication is correctly applied. The `IPersistableComponent` integration is correct. The Requirement System integration is properly optional.

The issues below are design flaws, architectural problems, or correctness bugs that should be resolved before or during implementation.

---

## Critical Issues

### 1. `OnLevelUp` / `OnXPChanged` Delegates Bound Inside `GrantXP` Per Call

**File:** `UProgressionSubsystem::GrantXP`

The spec binds `LevelingComp->OnLevelUp.AddUObject(...)` inside `GrantXP`, which runs on every XP grant. `AddUObject` on a `DYNAMIC_MULTICAST_DELEGATE` does not prevent duplicates — each call adds another binding. After 100 XP grants on the same actor, `HandleLevelUpForAudit` fires 100 times per level-up.

**Fix:** Bind the audit delegate once when the actor is registered with the subsystem (e.g. in `OnPlayerRegistered` or a dedicated `RegisterActor` method). Use `AddUnique` where possible, or gate the binding behind an `IsAlreadyBound` check.

---

### 2. Event Bus LevelUp Message Emitted Twice

**Files:** `ULevelingComponent::ProcessLevelUp`, `UProgressionSubsystem::GrantXP`

The spec describes `UProgressionSubsystem` broadcasting `GameCoreEvent.Progression.LevelUp` after `ApplyXP`, but `ProcessLevelUp` (inside the component) also calls `Bus->Broadcast(GameCoreEvent.Progression.LevelUp, ...)`. This creates a duplicate broadcast per level-up.

**Fix:** Choose one broadcast site. The recommended pattern is:
- Component fires **only the intra-system delegate** (`OnLevelUp`).
- Subsystem subscribes to `OnLevelUp` and broadcasts the full Event Bus message with `Instigator` populated.

This keeps the component free of `APlayerState` knowledge and ensures the message is always complete.

---

### 3. `Instigator` Is Null in Component-Emitted Event Bus Messages

**File:** `ULevelingComponent::ApplyXP`, `ProcessLevelUp`

The spec explicitly notes `Msg.Instigator = nullptr` in component-emitted messages, with a note that the subsystem "populates" it. But if the component broadcasts first (before the subsystem can intercept), all listeners receive a null `Instigator`. For `XPChanged`, this is especially problematic for quest/achievement systems that need to attribute the grant.

**Fix:** Follows from Issue 2 above. Component never broadcasts to the Event Bus. Subsystem broadcasts with all fields populated after `ApplyXP` returns.

---

## Design Flaws

### 4. `APlayerState*` Instigator Limits NPC-Sourced XP Grants

**File:** `UProgressionSubsystem::GrantXP`

`GrantXP` requires an `APlayerState*` Instigator, making it impossible to grant XP from a non-player source (e.g. a passive world event, server-side trigger, or NPC-to-NPC scenario) without fabricating a fake player state. This was flagged as PROG-F3 in the original spec.

**Fix (proposed):** Accept `AActor*` as Instigator and resolve multipliers only if it implements a `IProgressionInstigatorInterface` (or has an ASC). This keeps the API generic while preserving multiplier and audit attribution for players.

---

### 5. `FProgressionGrantDefinition` — Single Grant Per Level-Up

**File:** `ULevelProgressionDefinition::LevelUpGrant`

Only one point pool can receive a grant per level-up. This is sufficient for simple progressions but will block any design that grants into multiple pools (e.g. "level up grants 1 skill point AND 1 attribute point").

**Fix:** Change `FProgressionGrantDefinition LevelUpGrant` to `TArray<FProgressionGrantDefinition> LevelUpGrants` now. It is schema-breaking later. Cost is trivial at this stage.

---

### 6. `HandleLevelUpForAudit` Lacks Full Attribution

**File:** `UProgressionSubsystem::HandleLevelUpForAudit`

The audit entry fired in `HandleLevelUpForAudit` cannot include `Instigator` or `Target` because the delegate signature only carries `(ProgressionTag, NewLevel)`. The XPGain audit entry contains the full attribution, but level-up audit records are incomplete.

**Fix:** Add `Instigator` and `Target` to the `FOnProgressionLevelUp` delegate signature, or capture them at `GrantXP` call time and store them temporarily (e.g. in a per-frame `TMap` keyed by `LevelingComponent`) for the duration of the `ApplyXP` call.

---

### 7. `bAllowLevelDecrement` Level-Down Does Not Broadcast a Distinct Event

**File:** `ULevelingComponent::ProcessLevelDown`

`ProcessLevelDown` reuses `OnLevelUp` with a lower `NewLevel`, relying on listeners to compare `OldLevel` vs `NewLevel`. This is fragile — listeners written assuming `NewLevel > OldLevel` will silently misbehave (e.g. a quest system that checks `NewLevel >= 10` would trigger on decrement from 11→10).

**Fix:** Add a distinct `FOnProgressionLevelDown` delegate and a distinct `GameCoreEvent.Progression.LevelDown` Event Bus channel. Listeners opt-in to decrement handling explicitly.

---

## Minor Issues

### 8. `GetXPToNextLevel` Not Implemented in Spec

The method is declared on `ULevelingComponent` but its implementation is absent from the spec. It should delegate to `ULevelProgressionDefinition::GetXPRequiredForLevel(CurrentLevel) - CurrentXP`, clamped at 0.

---

### 9. FastArray Callbacks Not Fully Specified

`FProgressionLevelData` and `FPointPoolData` declare `PostReplicatedAdd`, `PostReplicatedChange`, and `PreReplicatedRemove` but their implementations are absent. These need to fire the appropriate delegates/Event Bus messages on the client to keep UI in sync with replicated state.

---

### 10. `UXPReductionPolicyCurve::Evaluate` — Null Curve Fallback Is Silent

If `ReductionCurve` is null (designer forgot to assign it), `Evaluate` silently returns `1.f` (no reduction). While this is a safe fallback, it can mask misconfigured assets in production. A `UE_LOG` warning in non-shipping builds would surface the issue during QA.

---

### 11. `ULevelProgressionDefinition` — Invalid XP Curve Returns 0, Causing Infinite Level-Up Loop

If `XPCurveFloat` is assigned but the `UCurveFloat` asset is null at runtime (unloaded/deleted), `GetXPRequiredForLevel` returns `0`. In `ApplyXP`, `while (CurrentXP >= XPNeeded)` becomes an infinite loop since `0 >= 0` is always true.

**Fix:** Guard with:
```cpp
const int32 XPNeeded = Def->GetXPRequiredForLevel(Data->CurrentLevel);
if (XPNeeded <= 0) break;  // Already present in spec — confirm this is enforced
```
The spec already includes `if (XPNeeded <= 0 || ...) break;` — ensure this survives implementation.

---

## Suggestions

### S-1: Add `TArray<FProgressionGrantDefinition> LevelUpGrants` Now
Single-grant limitation is a schema-breaking change later (see Issue 5). Do it now while the schema is fresh.

### S-2: Consider a `UProgressionComponent` Façade
Having callers look up both `ULevelingComponent` and `UPointPoolComponent` independently is verbose. A thin `UProgressionComponent` that owns both and exposes a unified API (`GetLevel`, `GetSpendable`, `AddPoints`) would reduce surface area without changing the underlying design.

### S-3: Progression Prerequisites Should Support `bGateXPGain` Flag
Currently prerequisites only gate `RegisterProgression`. For skill trees where a mastery requires prerequisites to also block XP accumulation (not just unlock), a `bGateXPGain` flag on `FProgressionPrerequisite` would avoid needing a separate system.

### S-4: `SerializeForSave` Versioning
Both components should embed a schema version integer at the top of their binary archive (e.g. `uint8 Version = 1`), and implement `Migrate(uint8 OldVersion, FArchive& Ar)` matching the pattern established by the Serialization System. Without versioning, any future field addition will corrupt existing save data silently.
