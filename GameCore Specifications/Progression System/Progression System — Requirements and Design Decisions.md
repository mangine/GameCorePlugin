# Progression System — Requirements & Design Decisions

This document records the functional requirements, non-functional requirements, and architectural decisions made during the design of the Progression System. It is the authoritative reference for **why** things are the way they are. Implementation specs live in the sibling pages.

---

## Functional Requirements

### FR-1 — Generic Progression, Not Just Skills
The system must represent any form of leveled progression: character level, skill mastery, crafting proficiency, reputation, faction standing, etc. The word "skill" is intentionally absent from all public API surface. Progressions are identified solely by `FGameplayTag`.

> **Decision:** `USkillDefinition` was renamed `ULevelProgressionDefinition`. `FSkillLevelData` became `FProgressionLevelData`. All tag fields use `ProgressionTag`, not `SkillTag`.

### FR-2 — Multiple Progressions Per Actor
An actor must be able to own and independently advance any number of progressions simultaneously (e.g. a pirate character that simultaneously levels Character.Level, Swordsmanship, Navigation, and PirateGuild.Reputation).

> **Decision:** One `ULevelingComponent` owns a `TArray<FProgressionLevelData>` (FastArray) keyed by tag. One component per skill was explicitly rejected — 20+ components per character is untenable at MMORPG scale (replication channels, tick registrations, memory).

### FR-3 — Flexible XP Curves
Different progressions must support different XP curves without code changes. Designers must be able to pick the curve strategy per-progression from the editor.

> **Decision:** `EXPCurveType` enum on `ULevelProgressionDefinition` with three modes:
> - `Formula` — `XP = Base × Level^Exponent` with optional `FRichCurve` multiplier overlay. Covers 95% of cases with zero asset overhead.
> - `CurveFloat` — `UCurveFloat` asset; level on X, XP on Y. For visually tuned progressions.
> - `CurveTable` — `FCurveTableRowHandle`; multiple progressions sharing one asset. For large games with many progressions.
>
> Callers use `GetXPRequiredForLevel(Level)` and never care which mode is active.

### FR-4 — Patchable Level Cap
The maximum level must be changeable per-patch without code changes.

> **Decision:** `MaxLevel` lives on `ULevelProgressionDefinition` (a `UDataAsset`). Raising the cap is a content change, not a code change.

### FR-5 — Point Grants on Level-Up
Leveling up must be able to grant points into named pools (skill points, attribute points, talent points, etc.). Points must not be hardcoded to a specific type.

> **Decision:** `FProgressionGrantDefinition` carries a `PoolTag (FGameplayTag)` identifying the destination pool and an `EGrantCurveType`-driven amount. This allows:
> - Different pools per progression (swordsmanship grants `Points.Skill`; navigation grants `Points.Navigation`).
> - Shared pools across progressions (two skills both grant into `Points.Skill`).
> - Scaling grants at higher levels via `CurveFloat` or `CurveTable`.

> **Rejected:** A `GrantType` enum was proposed but rejected — the pool tag already encodes type. Redundant information would drift out of sync.

### FR-6 — Point Pool Aggregation from Multiple Sources
Points must be grantable from sources other than leveling (quest rewards, events, achievements, GM grants, seasonal bonuses). The leveling component must not be the only way to grant points.

> **Decision:** `UPointPoolComponent` is a **fully standalone component** with no knowledge of leveling. It is the single aggregation point for all pool mutations. `ULevelingComponent` is just one caller among many.

### FR-7 — Available vs Consumed Tracking
Point pools must track the total lifetime points granted (`Available`) separately from points spent (`Consumed`). Spendable balance is always `Available - Consumed`.

> **Rationale:** Enables respec systems (zero `Consumed` without touching `Available`), audit capability, and clear UI representation of "earned" vs "spent".

> **Rejected:** A single `Balance` field was considered but rejected as it destroys audit and respec capability.

### FR-8 — Optional Pool Cap
Point pools may optionally enforce a maximum on `Available` to prevent hoarding (e.g. max 50 unspent skill points). A cap of `0` means no cap.

> **Decision:** `FPointPoolData::Cap` field, defaulting to `0`. `AddPoints` returns `EPointAddResult` so callers know if points were silently capped. `PartialCap` result should be logged by the caller as a designer configuration warning.

### FR-9 — Prerequisites for Unlocking a Progression
A progression may require other progressions to reach a minimum level before it can be unlocked on an actor.

> **Decision:** Two-tier prerequisite system on `ULevelProgressionDefinition`:
> 1. `TArray<FProgressionPrerequisite>` — fast-path struct check (tag + min level). No allocation. Checked first, short-circuits on failure.
> 2. `TArray<TObjectPtr<URequirement>>` — full `URequirement` evaluation from the Requirement System. Checked only if fast prerequisites pass.
>
> Prerequisites are checked in `RegisterProgression` (server-only). They do **not** gate XP gain mid-progression — that is outside scope.

### FR-10 — Negative XP Support (Reputation)
Progressions used for reputation must support XP subtraction (e.g. hostile actions reduce standing with a faction) without causing level regression.

> **Decision:** `CurrentXP` is `int32` (signed). On negative `AddXP`:
> - XP clamps at `0` (base of current level) — never goes below current level floor.
> - Level **never decreases**.
> - `OnXPChanged` fires with the clamped delta.
>
> This mirrors GW2 WvW rank, ESO faction standing: rank is permanent, XP reflects standing within current rank.

### FR-11 — Audit-Ready XP Source Identification
All XP grants must carry structured source identification for telemetry, exploit detection, and CS tooling. Source identity must be generic enough to represent mobs, quests, events, market transactions, and any future source type.

> **Decision:** `ISourceIDInterface` (in `GameCore Core`) is implemented by any UObject that can act as an XP source. Returns `FGameplayTag GetSourceTag()` (e.g. `Source.Mob.Skeleton.Level10`) and optional `FText GetSourceDisplayName()`. `AddXP` accepts `TScriptInterface<ISourceIDInterface>`. No internal buffer or ring buffer is enforced — the system provides the hookup point; the game layer wires it to the backend audit system.
>
> **Naming decision:** Originally proposed as `UXPSourceInterface`, renamed to `ISourceIDInterface` to make it reusable for drops, market logs, and any future system that needs source identification.

### FR-12 — Persistence Contract
Both components must expose a clean serialize/deserialize contract from day one. The game layer (not GameCore) is responsible for calling save/load and forwarding data to/from the backend.

> **Decision:** `SerializeToString() / DeserializeFromString(const FString&)` on both `ULevelingComponent` and `UPointPoolComponent`. Server-only. Compatible with `IPersistableComponent`.

---

## Non-Functional Requirements

### NFR-1 — Server Authority (Anti-Cheat)
All state mutations (`AddXP`, `RegisterProgression`, `AddPoints`, `ConsumePoints`) must be server-only. The client must never be able to modify progression or pool state directly.

> **Implementation:** All mutation methods are `BlueprintAuthorityOnly`. No `SetLevel` or `SetXP` is exposed — level is always a consequence of XP accumulation. Direct level injection is not possible.

### NFR-2 — Replication Efficiency
At MMORPG scale, a character may have 20+ progression tracks. Replication must be delta-compressed per-element, not full-array.

> **Implementation:** Both `FProgressionLevelDataArray` and `FPointPoolDataArray` use `FFastArraySerializer`. Only changed elements are sent per tick.

### NFR-3 — Zero Coupling Between Components
`UPointPoolComponent` must have zero knowledge of `ULevelingComponent`. Adding or removing either component from an actor must not break the other.

> **Implementation:** `ULevelingComponent` resolves `UPointPoolComponent` via `GetOwner()->FindComponentByClass` at level-up time. If absent, grant is silently skipped with a dev-build warning.

### NFR-4 — No Game-Specific Assumptions
GameCore is a generic library. No pirate-specific types, no hardcoded pool names, no hardcoded progression tag values belong in any GameCore class. All tag values are project-defined.

---

## Decisions Considered and Rejected

| Proposal | Decision | Reason |
|---|---|---|
| One component per skill/progression | Rejected | 20+ components per character: excessive replication channels, tick cost, memory |
| `GrantType` enum on grant definition | Rejected | Pool tag already encodes type; enum would create drift and redundancy |
| Internal circular buffer for audit records | Rejected | Adds complexity and memory pressure; hookup point (ISourceIDInterface) is sufficient; game layer owns audit |
| Single `Balance` field on point pools | Rejected | Destroys respec capability and audit trail |
| Level regression on negative XP | Rejected | Violates established MMORPG convention; confusing UX; level is a permanent achievement |
| XP anti-cheat validation inside the component | Deferred | GameCore provides the server-authoritative structure; exploit detection heuristics belong in game-layer or backend |

---

## Open / Future Items

| ID | Topic | Notes |
|---|---|---|
| PROG-F1 | Progression prerequisites gating XP gain | Currently only gates unlock. Mid-progression gating is out of scope but may be needed for advanced skill trees. |
| PROG-F2 | Multi-pool grants per level-up | Current design: one grant definition per progression. If a level-up needs to grant into two pools simultaneously, the schema will need `TArray<FProgressionGrantDefinition>`. Deferred — not required now. |
| PROG-F3 | `GrantXP` signature with `APlayerState*` Instigator | Current `UProgressionSubsystem::GrantXP` takes `APlayerState*` directly, limiting NPC reuse. Future refactor should accept a more generic target. |
| PROG-F4 | HISM Proxy Actor integration | Not applicable to this system. Progression is HISM-unaware by design. |
