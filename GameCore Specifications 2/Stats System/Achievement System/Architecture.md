# Achievement System — Architecture

**Part of:** Stats System | **Status:** Active Specification | **UE Version:** 5.7

---

## Overview

The Achievement System is a sub-system of the Stats System. It evaluates named achievements against one or more stat thresholds and an optional `URequirementList`. Achievements are server-authoritative, monotonic (never revoked), and persisted alongside stat data on `APlayerState`.

Evaluation is **triggered only by stats relevant to the achievement** — no global fan-out. `UStatDefinition` carries soft references to affected achievements; `UAchievementComponent` builds a `StatTag → achievements` lookup map at `BeginPlay`.

---

## Dependencies

### GameCore Plugin Systems

| System | Type | Usage |
|---|---|---|
| Stats System (`UStatComponent`, `FStatChangedEvent`) | **Required** | Stat value reads + change events |
| Event Bus (`UGameCoreEventBus2`) | **Required** | `FStatChangedEvent` listener + `FAchievementUnlockedEvent` broadcast |
| Serialization (`IPersistableComponent`) | **Required** | Earned set + requirement payload persistence |
| Requirement System (`URequirementWatcherComponent`, `URequirementList`) | **Optional** | Only when `AdditionalRequirements` is non-null |

### No Dependencies On

- Quest System, Progression System, Journal System (they subscribe to Event Bus)
- Any game-module code

---

## Requirements

- Achievements are identified by `FGameplayTag`.
- Each achievement is defined in a `UAchievementDefinition` DataAsset — no C++ required for threshold-only cases.
- **All stat thresholds must pass (AND semantics).** Most achievements have exactly one threshold.
- An optional `URequirementList` covers non-stat conditions (time of day, zone, etc.).
- Evaluation fires only for stats relevant to the achievement — no O(all achievements) fan-out.
- Achievements are monotonic — once earned, they cannot be revoked. Watcher handles for `AdditionalRequirements` are unregistered immediately after grant.
- Client receives a replicated earned-set (`FGameplayTagContainer`) for UI display. No write path on client.
- Stat progress is read live from `UStatComponent` — never duplicated.
- `URequirement_Persisted` data for achievements not covered by the stats system is stored in `UAchievementComponent` and persisted as `FRequirementPayload`.

---

## Features

- **Data-driven**: all achievements are DataAssets; no C++ needed for threshold-only achievements.
- **Efficient evaluation**: `StatTag → achievements` lookup map prevents scanning all achievements on every stat change.
- **Monotonic safety**: watcher unregistered on grant; `EarnedAchievements` membership checked before all evaluation paths.
- **Decoupled**: broadcasts `FAchievementUnlockedEvent`; rewards, audio, and UI subscribe independently.
- **Requirement watchers only when needed**: watcher overhead only paid for achievements with `AdditionalRequirements`.
- **Progress API**: `GetProgress()` reads live from `UStatComponent` — single source of truth, no staleness risk.
- **Replication**: `EarnedAchievements` replicated to owning client only (`COND_OwnerOnly`).

---

## Design Decisions

| Decision | Rationale |
|---|---|
| Stat → Achievement links on `UStatDefinition.AffectedAchievements` | Evaluation fires only for stats relevant to changed value; no O(all achievements) fan-out. |
| Soft references on `UStatDefinition.AffectedAchievements` | Prevents hard module dependency from Stats onto Achievement. |
| All thresholds AND | Simplest correct default; OR semantics modelled as separate `UAchievementDefinition` assets. |
| Watcher only for `AdditionalRequirements` | Stats cover vast majority of cases; watcher overhead only paid for complex conditions. |
| Stat re-check on watcher pass | Avoids caching threshold state in two places; float comparisons against `UStatComponent` are negligible. |
| Watcher unregistered after grant | Achievements are monotonic — continued watching is pure overhead. |
| `GetProgress` reads live from `UStatComponent` | Single source of truth; no staleness risk. |
| Persisted `FRequirementPayload` per achievement | Covers `URequirement_Persisted` needs without coupling GameCore to game-specific data shapes. |
| `InjectRequirementPayload` is game-module responsibility | GameCore stores and injects the payload; it never knows what's inside it. |
| `EarnedAchievements` as `FGameplayTagContainer` | O(1) membership, compact serialization, no custom container needed. |
| Replicated `FGameplayTagContainer` (`COND_OwnerOnly`) | Achievements unlock rarely; FastArray overhead not justified. |
| Lookup map built from `Definitions` array | Authoritative source; soft refs on `UStatDefinition` are for content-authoring assistance only. |

---

## Logic Flow

### Stat-Triggered Path

```
1. FStatChangedEvent fires on Event.Stat.Changed
2. UAchievementComponent::OnStatChanged(Event)
3. Lookup StatToAchievements[Event.StatTag]
4. For each UAchievementDefinition* Def:
   a. Skip if EarnedAchievements.HasTag(Def->AchievementTag)
   b. CheckStatThresholds(Def) — reads UStatComponent::GetStat() for each threshold
   c. If all thresholds pass:
      - No AdditionalRequirements → GrantAchievement()
      - Has AdditionalRequirements → check watcher's last cached result
        - If watcher last resolved true → GrantAchievement()
        - Else: wait for watcher
```

### Watcher-Triggered Path

```
1. URequirementWatcherComponent fires OnWatcherDirty(bAllPassed)
2. If !bAllPassed: return
3. If EarnedAchievements.HasTag(AchTag): return
4. CheckStatThresholds(Def) — re-check (watcher pass alone is not enough)
5. If thresholds pass → GrantAchievement()
```

### Grant Path

```
1. EarnedAchievements.AddTag(AchievementTag)
2. MarkDirty() — IPersistableComponent
3. UnregisterWatcher(AchievementTag) — no further watcher overhead
4. RequirementPayloads.Remove(AchievementTag) — clean up persisted payload
5. Broadcast FAchievementUnlockedEvent on GameCoreEvent.Achievement.Unlocked (ServerOnly)
6. DOREPLIFETIME replicates EarnedAchievements to owning client
7. Client: OnRep_EarnedAchievements → UI delegate
```

---

## Architecture

```
GameCore Plugin
├── UAchievementDefinition        (UDataAsset — one per achievement)
├── FStatThreshold                (USTRUCT — StatTag + threshold value)
├── FStatThresholdProgress        (USTRUCT — current/required for UI)
├── FAchievementUnlockedEvent     (EventBus payload)
├── UAchievementComponent         (UActorComponent on APlayerState, IPersistableComponent)
└── UStatDefinition               (extended: AffectedAchievements soft-ref array)

Game Module
├── UAchievementDefinition assets
└── GameCoreEvent.Achievement.Unlocked consumers (UI, audio, rewards)
```

---

## Known Issues

1. **Content sync burden**: content authors must keep `UStatDefinition.AffectedAchievements` and `UAchievementComponent.Definitions` in sync. A mismatch results in silent non-evaluation. The non-shipping validation in `BeginPlay` warns on mismatches but cannot prevent them.

2. **`GetProgress` on client**: `UAchievementComponent::GetProgress` calls `UStatComponent::GetStat`, which returns 0 on the client (values not replicated). Progress bars in client UI require a separate replicated property or RPC in the game module. GameCore does not prescribe this.

3. **`FindByPredicate` in hot paths**: `OnWatcherDirty` and `GetProgress` use `FindByPredicate` over the `Definitions` array. For projects with large achievement counts, a secondary `TMap<FGameplayTag, UAchievementDefinition*>` lookup cache at `BeginPlay` would eliminate these scans.

4. **No partial OR semantics**: all stat thresholds are AND. OR semantics require separate `UAchievementDefinition` assets. This is intentional but is a content authoring cost for complex unlock conditions.

---

## File Structure

```
GameCore/Source/GameCore/
└── Stats/
    └── Achievements/
        ├── AchievementComponent.h
        ├── AchievementComponent.cpp
        ├── AchievementDefinition.h
        ├── AchievementDefinition.cpp   (minimal — DataAsset, no logic)
        └── AchievementTypes.h          (FStatThreshold, FStatThresholdProgress, FAchievementUnlockedEvent)
```
