# Progression System

## Overview

The Progression System is a **GameCore plugin system** that manages any form of leveled progression — character level, skill mastery, reputation, faction standing, or any other metric that advances through accumulated XP. It is fully data-driven, server-authoritative, and designed to integrate with GAS gameplay tags throughout.

The system is composed of two decoupled components that live on the same Actor:

| Component | Responsibility |
| --- | --- |
| `ULevelingComponent` | Owns progression states (XP, level per tag). Fires level-up grants. |
| `UPointPoolComponent` | Owns point pools (available vs consumed per pool tag). Accepts grants from any source. |

They communicate in one direction only: `ULevelingComponent` calls into `UPointPoolComponent` on level-up. `UPointPoolComponent` has no knowledge of leveling.

## Where It Lives

```
GameCore/
└── Source/
    └── GameCore/
        └── Progression/
            └── LevelingComponent.h / .cpp
            └── PointPoolComponent.h / .cpp
            └── LevelProgressionDefinition.h / .cpp
            └── ProgressionTypes.h
```

## Key Design Decisions

- **Single component, multi-progression** — one `ULevelingComponent` owns a `TArray` (FastArray) keyed by `FGameplayTag`. No per-skill components.
- **Tag-keyed everything** — progressions, point pools, XP sources, and grants are all identified by `FGameplayTag`, enabling maximum flexibility and GAS integration.
- **Data-driven definitions** — each progression type is defined in a `ULevelProgressionDefinition` DataAsset, not in code.
- **Decoupled point pools** — `UPointPoolComponent` is standalone. Events, quests, achievements, and GM tools can all grant points without touching the leveling component.
- **Server-authoritative** — all mutations (`AddXP`, `AddPoints`, `ConsumePoints`) are server-only. The client receives replicated state and delegates only.
- **Audit-ready** — `AddXP` accepts a `TScriptInterface<ISourceIDInterface>` for structured source identification, ready for backend telemetry hookup.

## Sub-pages

See the sub-pages below for full component and type specifications.

[Progression Types & Data Assets](Progression%20System/Progression%20Types%20&%20Data%20Assets%2031bd261a36cf81e986c7d0d4e6d95e34.md)

[ULevelingComponent](Progression%20System/ULevelingComponent%2031bd261a36cf81fd8a12f8b8c8d5d92b.md)

[UPointPoolComponent](Progression%20System/UPointPoolComponent%2031bd261a36cf81bdb4b7ea52efe6d145.md)