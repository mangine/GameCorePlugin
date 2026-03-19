# Stats System

## Overview

The Stats System is a server-authoritative, data-driven player statistics tracker. It accumulates numerical values per player using `FGameplayTag`-keyed counters, persists them to the game database, and broadcasts changes so other systems (Achievements, Leaderboards, Quests) can react without coupling to this one.

All stat mutation happens server-side only. The client never writes stats.

---

## Key Requirements

- Stats are identified by `FGameplayTag`.
- Each stat is defined in a `UStatDefinition` DataAsset — no C++ required per stat.
- Stats auto-register GMS listeners based on authored `UStatIncrementRule` objects on `BeginPlay`.
- Increment logic is encapsulated in `UStatIncrementRule` subclasses; payload extraction uses `FInstancedStruct`.
- A `URequirementSet` gates whether a stat increment is applied at all.
- A single stat can be fed by multiple sources (multiple rules).
- Runtime values live in `UStatComponent` on `APlayerState`, which implements `IPersistableComponent`.
- Persistence uses dirty-flag + flush pattern — not per-increment writes.
- Stat changes are broadcast via the Event Bus so downstream systems subscribe without direct coupling.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| `FInstancedStruct` for GMS payloads | Allows editor-configured, type-safe payload dispatch without compile-time type knowledge in the DataAsset |
| `UStatIncrementRule` as `EditInlineNew` UObject | Fully editor-configured increment logic; one subclass per message shape, reused across all stats of that shape |
| Rules live on `UStatDefinition`, not on the component | Stat identity and tracking logic are co-located in one authorable asset |
| `URequirementSet` per definition | Reuses existing Requirement System; no new gating mechanism needed |
| Dirty-flag + flush persistence | High-frequency stats (damage, hits) would be too expensive to persist per-increment |
| Server-only mutation | Anti-cheat; client has no write path |
| Event Bus broadcast on change | Decouples Achievement/Quest systems from Stats entirely |

---

## Architecture

```
GameCore Plugin
├── UStatComponent           (UActorComponent, on APlayerState, server-only writes)
├── UStatDefinition          (UDataAsset — one per tracked stat)
├── UStatIncrementRule       (abstract UObject, EditInlineNew)
│   └── UConstantIncrementRule   (GameCore built-in)
└── FStatChangedEvent        (Event Bus payload)

Game Module
├── UDamageIncrementRule     (example: extracts float from FDamageDealtMessage)
├── UStatDefinition assets   (content — one DataAsset per stat)
└── (no per-stat C++ required after rules are authored)
```

---

## Module Pages

- [UStatDefinition](./UStatDefinition.md)
- [UStatIncrementRule](./UStatIncrementRule.md)
- [UStatComponent](./UStatComponent.md)
- [FStatChangedEvent & Integration](./Integration.md)

---

## Quick-Start Guide

### 1. Create a stat that increments by 1 on a GMS event

1. Create a `UStatDefinition` DataAsset, e.g. `DA_Stat_EnemiesKilled`.
2. Set `StatTag` = `Stat.Player.EnemiesKilled`.
3. Add one entry to `Rules`: pick `UConstantIncrementRule`.
   - Set `ChannelTag` = `GameplayMessage.Combat.EnemyKilled`.
   - Set `Amount` = `1.0`.
4. Leave `TrackingRequirements` empty (always tracks).
5. Add `DA_Stat_EnemiesKilled` to the `UStatComponent`'s `Definitions` array on the `APlayerState` Blueprint.

Done. No C++.

### 2. Create a stat that extracts a float from a payload

1. In the game module, subclass `UStatIncrementRule` as `UDamageIncrementRule` (see [UStatIncrementRule](./UStatIncrementRule.md) for pattern).
2. In `ExtractIncrement`, cast the `FInstancedStruct` payload to `FDamageDealtMessage` and return `Msg.DamageAmount`.
3. Create `DA_Stat_TotalDamageDealt` DataAsset, add a `UDamageIncrementRule` entry.

### 3. Gate a stat behind a requirement

On the `UStatDefinition`, set `TrackingRequirements` to a `URequirementSet` asset that checks e.g. player level >= 10. The stat will not increment until requirements are met.

### 4. React to a stat change (e.g. Achievement System)

```cpp
// In AchievementSubsystem::Initialize()
UGameCoreEventBus::Get(this).Subscribe<FStatChangedEvent>(
    TAG_Event_StatChanged,
    EGameCoreEventScope::Server,
    this,
    &UAchievementSubsystem::OnStatChanged
);

void UAchievementSubsystem::OnStatChanged(const FStatChangedEvent& Event)
{
    // Event.StatTag, Event.NewValue, Event.PlayerId
    EvaluateAchievements(Event.PlayerId, Event.StatTag, Event.NewValue);
}
```

### 5. Manually increment a stat (non-event-driven)

```cpp
// Server only. E.g. quest completion grants a stat.
if (UStatComponent* Stats = PlayerState->FindComponentByClass<UStatComponent>())
{
    Stats->AddToStat(TAG_Stat_QuestsCompleted, 1.f);
}
```
