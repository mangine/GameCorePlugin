# Stats System

## Overview

The Stats System is a server-authoritative, data-driven player statistics tracker. It accumulates numerical values per player using `FGameplayTag`-keyed counters, persists them to the game database, and broadcasts changes so other systems (Achievements, Leaderboards, Quests) can react without coupling to this one.

All stat mutation happens server-side only. The client never writes stats.

---

## Key Requirements

- Stats are identified by `FGameplayTag`.
- Each stat is defined in a `UStatDefinition` DataAsset — no C++ required per stat for fixed-increment cases.
- `UStatComponent` auto-registers `UGameCoreEventBus2` listeners at `BeginPlay` from authored `UStatIncrementRule` objects. No game-module wiring subsystem needed.
- Increment logic and payload extraction are encapsulated in `UStatIncrementRule::ExtractIncrement(FInstancedStruct)` — overrideable in C++ or Blueprint.
- A `URequirementSet` gates whether a stat increment is applied at all.
- A single stat can be fed by multiple rules (multiple channels).
- Runtime values live in `UStatComponent` on `APlayerState`, which implements `IPersistableComponent`.
- Persistence uses dirty-flag + flush pattern — not per-increment writes.
- Stat changes are broadcast via `UGameCoreEventBus2` so downstream systems subscribe without direct coupling.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| `FInstancedStruct` payload via EventBus2 | Enables auto-registration without compile-time type knowledge in the DataAsset. Rules extract typed data themselves via `GetPtr<T>()`. |
| `UStatIncrementRule::ExtractIncrement` owns payload extraction | Single override point for both C++ and Blueprint subclasses. Keeps `UStatComponent` generic. |
| `UStatComponent` auto-registers listeners | Eliminates the game-module wiring subsystem. All event-driven wiring is data-driven from the authored definitions. |
| One listener per `(ChannelTag, StatTag)` pair, no deduplication | Same total call count as deduplication + fan-out. Simpler — no `TMultiMap` bookkeeping. |
| `UStatIncrementRule` as `EditInlineNew` UObject | Fully editor-configured increment logic; one subclass per message shape, reused across stats. |
| Rules live on `UStatDefinition`, not on the component | Stat identity and tracking logic are co-located in one authorable asset. |
| `URequirementSet` per definition | Reuses existing Requirement System; no new gating mechanism needed. |
| Dirty-flag + flush persistence | High-frequency stats (damage, hits) would be too expensive to persist per-increment. |
| Server-only mutation | Anti-cheat; client has no write path. |
| EventBus2 broadcast on change | Decouples Achievement/Quest systems from Stats entirely. |

---

## Architecture

```
GameCore Plugin
├── UStatComponent           (UActorComponent on APlayerState — auto-registers EventBus2 listeners)
├── UStatDefinition          (UDataAsset — one per tracked stat)
├── UStatIncrementRule       (abstract UObject, EditInlineNew)
│   └── UConstantIncrementRule   (GameCore built-in — fixed amount, ignores payload)
└── FStatChangedEvent        (EventBus2 payload — broadcast on every stat change)

Game Module
├── UDamageIncrementRule     (extracts float from FDamageDealtMessage via GetPtr<T>)
├── UStatDefinition assets   (content — one DataAsset per stat)
└── (no wiring subsystem needed for event-driven stats)
```

---

## Module Pages

- [UStatDefinition](./UStatDefinition.md)
- [UStatIncrementRule](./UStatIncrementRule.md)
- [UStatComponent](./UStatComponent.md)
- [FStatChangedEvent & Integration](./Integration.md)

---

## Quick-Start Guide

### 1. Stat that increments by 1 on a GMS2 event

1. Create a `UStatDefinition` DataAsset, e.g. `DA_Stat_EnemiesKilled`.
2. Set `StatTag` = `Stat.Player.EnemiesKilled`.
3. Add one entry to `Rules`: pick `UConstantIncrementRule`.
   - Set `ChannelTag` = `GameplayMessage.Combat.EnemyKilled`.
   - Set `Amount` = `1.0`.
4. Leave `TrackingRequirements` empty.
5. Add `DA_Stat_EnemiesKilled` to the `UStatComponent`'s `Definitions` array on the `APlayerState` Blueprint.

Done. No C++. The component auto-registers the listener at `BeginPlay`.

### 2. Stat that extracts a float from a payload

1. In the game module, subclass `UStatIncrementRule` as `UDamageIncrementRule`.
2. Override `ExtractIncrement`: cast the payload with `GetPtr<FDamageDealtMessage>()` and return `Msg->DamageAmount`.
3. Create `DA_Stat_TotalDamageDealt`, add a `UDamageIncrementRule` entry, set `ChannelTag`.

No game-module subscription code needed.

### 3. Gate a stat behind a requirement

On the `UStatDefinition`, set `TrackingRequirements` to a `URequirementSet` asset. The stat will not increment until requirements are met.

### 4. React to a stat change (Achievement System)

```cpp
StatHandle = Bus->StartListening<FStatChangedEvent>(
    TAG_Event_StatChanged,
    this,
    [this](FGameplayTag, const FStatChangedEvent& Event)
    {
        EvaluateAchievements(Event.PlayerId, Event.StatTag, Event.NewValue);
    });
```

### 5. Manually increment a stat (non-event-driven)

```cpp
// Server only.
Stats->AddToStat(TAG_Stat_QuestsCompleted, 1.f);
```
