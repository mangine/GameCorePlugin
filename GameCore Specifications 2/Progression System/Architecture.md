# Progression System — Architecture

## Overview

The Progression System is a **server-authoritative, data-driven** GameCore module that manages any form of leveled progression — character level, skill mastery, faction reputation, crafting proficiency, or any metric that advances through accumulated XP. It is tag-keyed throughout for full GAS integration and maximum flexibility.

All external state mutations go through `UProgressionSubsystem` (the policy layer). Components are pure state owners; they never accept raw XP from external callers directly.

---

## Dependencies

### Unreal Engine Modules
- `Engine` — `UActorComponent`, `UWorldSubsystem`, `AActor`, `APlayerState`
- `GameplayTags` — `FGameplayTag` used for progression identity, pool identity, source identity
- `GameplayAbilities` — `UAbilitySystemComponent` queried for personal XP multiplier attribute (soft dependency, skipped if absent)
- `CoreUObject` — `UInterface`, `UDataAsset`, `UCurveFloat`, `FCurveTableRowHandle`

### GameCore Plugin Systems

| System | Usage | Hard/Soft |
|---|---|---|
| **Serialization System** (`IPersistableComponent`) | Both components implement binary save/load | Hard |
| **Requirement System** (`URequirement`) | Advanced prerequisites on `ULevelProgressionDefinition` | Soft |
| **Event Bus** (`UGameCoreEventBus`) | XP and level-up events broadcast to all external consumers | Hard |
| **GameCore Backend** (`FGameCoreBackend`) | Audit dispatch on XP grant and level-up | Hard |
| **GameCore Core** (`ISourceIDInterface`) | Structured XP source identification for audit | Hard |

### External (Game Module)
- Watcher/requirement adapters listen to `GameCoreEvent.Progression.LevelUp` on the Event Bus — **not** a compile-time dependency of this module.

---

## Requirements

### Functional

| ID | Requirement |
|---|---|
| FR-1 | Any form of leveled progression — not just character level or skills. All progressions identified by `FGameplayTag`. |
| FR-2 | Multiple simultaneous progressions per actor via a single `ULevelingComponent`. |
| FR-3 | Flexible XP curves per progression: formula, `UCurveFloat`, or `FCurveTableRowHandle`. Designers configure in-editor. |
| FR-4 | Patchable level cap — `MaxLevel` lives on a `UDataAsset`, not in code. |
| FR-5 | Level-up grants points into named pools, with curve-driven amounts per level. |
| FR-6 | Point pools accept grants from any source — quests, events, GMs, leveling. `UPointPoolComponent` is fully standalone. |
| FR-7 | `Available` vs `Consumed` tracking for respec support, audit, and UI. |
| FR-8 | Optional pool cap — `Cap == 0` means uncapped. `AddPoints` returns `EPointAddResult`. |
| FR-9 | Prerequisite system for unlocking progressions: fast-path struct check + optional `URequirement` evaluation. |
| FR-10 | Negative XP support for reputation tracks. Level never decrements by default; opt-in via `bAllowLevelDecrement`. |
| FR-11 | Structured XP source identification via `ISourceIDInterface` for telemetry and exploit detection. |
| FR-12 | Both components implement `IPersistableComponent` for binary save/load via the Serialization System. |

### Non-Functional

| ID | Requirement |
|---|---|
| NFR-1 | All state mutations are server-only (`BlueprintAuthorityOnly`). No `SetLevel`/`SetXP` exposed. |
| NFR-2 | `FFastArraySerializer` on both arrays — delta-compressed per-element at MMORPG scale (20+ progressions per actor). |
| NFR-3 | Zero coupling between `ULevelingComponent` and `UPointPoolComponent`. |
| NFR-4 | No game-specific assumptions (no pirate types, no hardcoded tag values) anywhere in GameCore. |

---

## Features

- Tag-keyed multi-progression tracking on a single component
- Three XP curve modes: parametric formula, `UCurveFloat`, `FCurveTableRowHandle`
- Per-level point grants into named pools, with curve-driven amounts
- Level-gap XP reduction via swappable `UXPReductionPolicy` (default: curve-based)
- Standalone point pool component — accepts grants from any system
- Available vs Consumed accounting on pools (respec, audit, UI)
- Optional pool cap with `EPointAddResult` return value
- Two-tier prerequisites: fast struct check + full `URequirement` evaluation
- Opt-in level decrement for reputation/PvP tracks
- Global XP multiplier + per-player GAS attribute multiplier (double-XP events)
- Audit dispatch via `FGameCoreBackend` with full `Instigator`/`Target` attribution
- Delta-replicated state via `FFastArraySerializer` on both components
- Binary persistence via `IPersistableComponent` (Serialization System)
- JSON helpers (`SerializeToString`/`DeserializeFromString`) for GM tooling only

---

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| One component, multiple progressions | `TArray<FProgressionLevelData>` (FastArray) in one `ULevelingComponent` | 20+ components per character is untenable at MMORPG scale — excess replication channels, tick registrations, memory |
| Tag-keyed everything | `FGameplayTag` for progressions, pools, sources | GAS integration, maximum flexibility, zero hardcoded names |
| Subsystem as policy layer | `UProgressionSubsystem` is the sole XP entry point | Multiplier resolution and audit live in the subsystem; components remain pure state owners |
| `UPointPoolComponent` fully standalone | No reference to `ULevelingComponent` | Quests, events, GMs all grant points without touching leveling |
| `Instigator` / `Target` split | `GrantXP(APlayerState* Instigator, AActor* Target, ...)` | Multipliers are always player-driven; XP can target any Actor with a `ULevelingComponent` |
| Component applies final XP | Subsystem resolves war XP (base × multipliers); component applies `UXPReductionPolicy` | Policy and state are cleanly separated |
| `bAllowLevelDecrement` | Off by default — XP floors at 0, level is permanent | Matches established MMORPG convention (GW2, ESO). Opt-in for PvP/bounty systems. |
| `UXPReductionPolicy` as instanced `UDataAsset` | `Instanced` on `ULevelProgressionDefinition` | Designers pick the policy in-editor per progression; custom policies require no subsystem changes |
| Audit hookup point | `ISourceIDInterface` on source, `FGameCoreBackend::GetAudit()` | GameCore provides structure; game layer owns exploit heuristics |
| JSON helpers as debug-only | `SerializeToString`/`DeserializeFromString` are `BlueprintAuthorityOnly`, never on the save path | Binary `FArchive` is the production path; JSON is for GM console and live-ops tooling |
| Watcher decoupled via Event Bus | Watcher adapter listens to `GameCoreEvent.Progression.LevelUp` on GMS | Progression module has no compile-time dependency on requirement watchers |

### Rejected Proposals

| Proposal | Reason |
|---|---|
| One component per skill/progression | 20+ components per character — excessive replication channels, tick cost, memory |
| `GrantType` enum on grant definition | Pool tag already encodes type; enum would create drift and redundancy |
| Internal circular buffer for audit records | Adds complexity; hookup point (`ISourceIDInterface`) is sufficient |
| Single `Balance` field on point pools | Destroys respec capability and audit trail |
| Level regression on negative XP (default) | Violates established MMORPG convention; level is a permanent achievement |
| Watcher notification directly from `UProgressionSubsystem` | Creates coupling between Progression and Requirement modules |

---

## Logic Flow

```
[Gameplay Code / RewardResolver]
    UProgressionSubsystem::GrantXP(Instigator, Target, Tag, BaseAmount, ContentLevel, Source)
    │
    ├─ Target = (Target != nullptr) ? Target : Instigator->GetPawn()
    ├─ LevelingComp = Target->FindComponentByClass<ULevelingComponent>()  ← silent no-op if absent
    ├─ ResolveMultiplier(Instigator)
    │     GlobalXPMultiplier × GAS personal multiplier on Instigator's ASC
    │     → WarXP = BaseAmount × CombinedMultiplier
    │
    ▼
    ULevelingComponent::ApplyXP(Tag, WarXP, ContentLevel)  [server-only, internal]
    │  Def->ReductionPolicy->Evaluate(CurrentLevel, ContentLevel)  → reduction multiplier
    │  FinalXP = WarXP × reduction
    │  Mutates FProgressionLevelDataArray (FastArray replication)
    │  ├─ OnXPChanged delegate  (intra-system: subsystem reads LastAppliedXPDelta)
    │  ├─ Event Bus: GameCoreEvent.Progression.XPChanged
    │  └─ On level-up or level-down:
    │       ProcessLevelUp / ProcessLevelDown
    │       ├─ GrantPointsForLevel → UPointPoolComponent::AddPoints(PoolTag, Amount)
    │       ├─ OnLevelUp delegate  (intra-system: subsystem audit)
    │       └─ Event Bus: GameCoreEvent.Progression.LevelUp
    │             └─ External listeners (no Progression module involvement):
    │                   Watcher adapter, Quest system, Achievement system, UI/VFX
    │
    ▼ (back in GrantXP, after ApplyXP returns)
    FGameCoreBackend::GetAudit(TAG_Audit_Progression)->RecordEvent(...)
    Payload: Instigator, Target, ProgressionTag, BaseAmount, WarXP, FinalXP, Source, ContentLevel
```

### UPointPoolComponent Direct Grant Flow (non-leveling source)
```
[Quest / Event / GM]
    UPointPoolComponent::AddPoints(PoolTag, Amount)
    │  Mutates FPointPoolDataArray (FastArray replication)
    ├─ OnPoolChanged delegate  (intra-system)
    └─ Event Bus: GameCoreEvent.Progression.PointPoolChanged
```

---

## Known Issues

| ID | Issue | Notes |
|---|---|---|
| PROG-1 | `GrantXP` takes `APlayerState*` Instigator directly, limiting NPC-to-NPC XP grant scenarios | For pure NPC sources, a refactor to accept a more generic target is needed. See PROG-F3. |
| PROG-2 | `OnPlayerRegistered` in `UProgressionSubsystem` binds audit only to the player's own pawn. If XP is granted to an NPC `Target`, the audit delegate may not be bound on that actor's `LevelingComponent`. | Bind audit on the component resolved during `GrantXP` rather than only at player registration. |
| PROG-3 | `FProgressionGrantDefinition` supports only a single grant per level-up | If a level-up needs to grant into two pools simultaneously, the schema needs `TArray<FProgressionGrantDefinition>`. Currently deferred. |
| PROG-4 | `ApplyXP` sets `Msg.Instigator = nullptr` then fires the Event Bus. The subsystem currently cannot inject `Instigator` into the component's in-flight broadcast without a refactor. | Preferred fix: subsystem broadcasts the GMS message itself after `ApplyXP` returns, using `LastAppliedXPDelta`. Component broadcasts only the intra-system delegate. |

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Progression/
            ├── ProgressionTypes.h                 # Enums, structs, FastArray wrappers
            ├── LevelProgressionDefinition.h/.cpp  # ULevelProgressionDefinition DataAsset
            ├── XPReductionPolicy.h/.cpp           # UXPReductionPolicy + UXPReductionPolicyCurve
            ├── LevelingComponent.h/.cpp           # ULevelingComponent
            ├── PointPoolComponent.h/.cpp          # UPointPoolComponent
            └── ProgressionSubsystem.h/.cpp        # UProgressionSubsystem
```
