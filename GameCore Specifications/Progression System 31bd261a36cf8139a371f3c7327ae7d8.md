# Progression System

## Overview

The Progression System is a **GameCore plugin system** that manages any form of leveled progression — character level, skill mastery, reputation, faction standing, or any other metric that advances through accumulated XP. It is fully data-driven, server-authoritative, and designed to integrate with GAS gameplay tags throughout.

The system is composed of two decoupled components that live on the same Actor, coordinated by a server-only world subsystem:

| Component / Class | Responsibility |
| --- | --- |
| `ULevelingComponent` | Owns progression states (XP, level per tag). Applies final XP after reduction. Fires level-up grants. |
| `UPointPoolComponent` | Owns point pools (available vs consumed per pool tag). Accepts grants from any source. |
| `UProgressionSubsystem` | Server-only entry point. Resolves multipliers, dispatches audit via `FGameCoreBackend`, notifies the Watcher system. |
| `UXPReductionPolicy` | Abstract base for level-gap XP reduction policies. `UXPReductionPolicyCurve` is the default curve-based implementation. |

Components communicate in one direction only: `ULevelingComponent` calls into `UPointPoolComponent` on level-up. `UPointPoolComponent` has no knowledge of leveling. All external XP grants flow through `UProgressionSubsystem`.

## Where It Lives

```
GameCore/
└── Source/
    └── GameCore/
        └── Progression/
            ├── LevelingComponent.h / .cpp
            ├── PointPoolComponent.h / .cpp
            ├── ProgressionSubsystem.h / .cpp
            ├── XPReductionPolicy.h / .cpp
            ├── LevelProgressionDefinition.h / .cpp
            └── ProgressionTypes.h
```

## Key Design Decisions

- **Single component, multi-progression** — one `ULevelingComponent` owns a `TArray` (FastArray) keyed by `FGameplayTag`. No per-skill components.
- **Tag-keyed everything** — progressions, point pools, XP sources, and grants are all identified by `FGameplayTag`, enabling maximum flexibility and GAS integration.
- **Data-driven definitions** — each progression type is defined in a `ULevelProgressionDefinition` DataAsset, not in code.
- **Decoupled point pools** — `UPointPoolComponent` is standalone. Events, quests, achievements, and GM tools can all grant points without touching the leveling component.
- **Server-authoritative** — all mutations are server-only. The client receives replicated state and delegates only.
- **Subsystem as policy layer** — `UProgressionSubsystem` is the sole external entry point for XP grants. Components remain pure state owners; all policy (multipliers, audit, watcher notification) lives in the subsystem.
- **Component applies final XP** — the subsystem resolves war XP (base × multipliers); the component applies the per-progression `UXPReductionPolicy` and produces the final amount. Server authority is preserved throughout.
- **Audit via FGameCoreBackend** — `UProgressionSubsystem` calls `FGameCoreBackend::GetAudit(TAG_Audit_Progression)` post-mutation. No custom audit interface.

## XP Grant Flow

```
[Gameplay Code / RewardResolver]
    UProgressionSubsystem::GrantXP(PS, ProgressionTag, BaseAmount, ContentLevel, Source)
    │
    ├─ ResolveMultipliers: GlobalXPMultiplier × GAS personal multiplier (ASC attribute)
    │  → WarXP = BaseAmount × CombinedMultiplier
    │
    ▼
    ULevelingComponent::ApplyXP(ProgressionTag, WarXP, ContentLevel)  ← internal, server-only
    │  Samples Definition->ReductionPolicy->Evaluate(PlayerLevel, ContentLevel)
    │  FinalXP = WarXP × ReductionMultiplier
    │  Pure state mutation — FastArray replication kicks in
    │  OnLevelUp / OnXPChanged delegates fire
    │
    ▼ (back in GrantXP, after ApplyXP returns)
    DispatchAudit → FGameCoreBackend::GetAudit(TAG_Audit_Progression)->RecordEvent(...)
    NotifyWatcher → URequirementWatcherManager::NotifyPlayerEvent(PS, TAG_RequirementEvent_Progression)
```

## Sub-pages

- [Progression Types & Data Assets](Progression%20System/Progression%20Types%20%26%20Data%20Assets%2031bd261a36cf81e986c7d0d4e6d95e34.md)
- [ULevelingComponent](Progression%20System/ULevelingComponent%2031bd261a36cf81fd8a12f8b8c8d5d92b.md)
- [UPointPoolComponent](Progression%20System/UPointPoolComponent%2031bd261a36cf81bdb4b7ea52efe6d145.md)
- [UProgressionSubsystem](Progression%20System/UProgressionSubsystem.md)
- [UXPReductionPolicy](Progression%20System/UXPReductionPolicy.md)
