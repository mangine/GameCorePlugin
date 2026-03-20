# Achievement System

**Part of: Stats System** | **Status: Active Specification** | **UE Version: 5.7**

The Achievement System is an extension of the Stats System. It evaluates named achievements against one or more stat thresholds and an optional `URequirementList`. Achievements are server-authoritative, monotonic (never revoked), and persisted alongside stat data on `APlayerState`.

---

## Key Requirements

- Achievements are identified by `FGameplayTag`.
- Each achievement is defined in a `UAchievementDefinition` DataAsset — no C++ required for threshold-only cases.
- **All stat thresholds must pass (AND).** Most achievements have exactly one threshold.
- An optional `URequirementList` (via the Requirement System watcher) covers non-stat conditions.
- Evaluation is **triggered only by stats relevant to the achievement** — no global fan-out. `UStatDefinition` carries soft references to affected achievements; `UAchievementComponent` builds a `StatTag → achievements` lookup map at `BeginPlay`.
- Watcher handles for `AdditionalRequirements` are registered at `BeginPlay` and **unregistered immediately after grant** — achievements are monotonic.
- `URequirement_Persisted` data specific to an achievement (not tracked by the stats system) is stored in `UAchievementComponent` and persisted as `FRequirementPayload`.
- Client receives a replicated earned-set for display. No write path on client.
- Stat progress is read live from `UStatComponent` — never duplicated.

---

## Architecture

```
GameCore Plugin
├── UAchievementDefinition          (UDataAsset — one per achievement)
├── FStatThreshold                  (USTRUCT — StatTag + threshold value)
├── FStatThresholdProgress          (USTRUCT — current/required for UI)
├── UAchievementComponent           (UActorComponent on APlayerState, IPersistableComponent)
├── FAchievementUnlockedEvent       (EventBus payload, channel: GameCoreEvent.Achievement.Unlocked)
└── UStatDefinition                 (extended: AffectedAchievements soft-ref array)

Game Module
├── UAchievementDefinition assets   (content — one DataAsset per achievement)
└── GameCoreEvent.Achievement.Unlocked consumers (UI, audio, rewards)
```

---

## Design Decisions

| Decision | Rationale |
|---|---|
| Stat → Achievement links on `UStatDefinition` | Evaluation fires only for stats relevant to the changed value; no O(all achievements) fan-out |
| Soft references on `UStatDefinition.AffectedAchievements` | Prevents a hard module dependency from Stats onto Achievement |
| All stat thresholds are AND | Simplest correct default; OR semantics are modelled as separate `UAchievementDefinition` assets |
| Watcher only for `AdditionalRequirements` | Stats cover the vast majority of cases; watcher overhead only paid for complex conditions |
| Stat threshold re-check on watcher pass | Avoids caching threshold state in two places; float comparisons against `UStatComponent` are negligible |
| Watcher unregistered after grant | Achievements are monotonic — continued watching is pure overhead |
| `GetProgress` reads live from `UStatComponent` | Single source of truth; no staleness risk |
| Persisted `FRequirementPayload` per achievement | Covers `URequirement_Persisted` needs without coupling GameCore to game-specific data shapes |
| `InjectRequirementPayload` is game-module responsibility | GameCore stores and injects the payload; it never knows what's inside it |
| `EarnedAchievements` as `FGameplayTagContainer` | O(1) membership, compact serialization, no custom container needed |
| Replication via replicated `FGameplayTagContainer` | Achievements unlock rarely; FastArray overhead not justified |

---

## Module Pages

- [UAchievementDefinition & Types](./UAchievementDefinition.md)
- [UAchievementComponent](./UAchievementComponent.md)
- [Integration & Events](./Integration.md)

---

## Quick-Start Guide

### 1. Single-stat achievement (most common case)

1. Open `DA_Stat_EnemiesKilled`. Add `DA_Achievement_KillEnemy100` to `AffectedAchievements`.
2. Create `DA_Achievement_KillEnemy100` (`UAchievementDefinition`):
   - `AchievementTag` = `Achievement.Combat.KillEnemy100`
   - `StatThresholds[0]`: `StatTag` = `Stat.Player.EnemiesKilled`, `Threshold` = `100`
   - `AdditionalRequirements` = none
3. Add `DA_Achievement_KillEnemy100` to `UAchievementComponent.Definitions` on the `APlayerState` Blueprint.

Done. No C++.

### 2. Multi-stat achievement

```
DA_Achievement_WarriorOfTheSeas
  AchievementTag:  Achievement.Combat.WarriorOfTheSeas
  StatThresholds:
    [0] StatTag: Stat.Player.EnemiesKilled   Threshold: 500
    [1] StatTag: Stat.Player.DamageDealt     Threshold: 100000
  AdditionalRequirements: (none)
```

Add this asset to `AffectedAchievements` on **both** `DA_Stat_EnemiesKilled` and `DA_Stat_DamageDealt`. Evaluation fires whenever either stat changes, but all thresholds must pass before the achievement grants.

### 3. Achievement with additional requirements

```
DA_Achievement_NightPirate
  AchievementTag:  Achievement.World.NightPirate
  StatThresholds:
    [0] StatTag: Stat.Player.EnemiesKilledAtNight  Threshold: 50
  AdditionalRequirements: RL_IsNightTime   // URequirementList: checks world time tag
```

The watcher on `RL_IsNightTime` is registered at `BeginPlay`. Achievement grants only when both the stat threshold and the requirement list pass simultaneously.

### 4. Achievement requiring persisted requirement data

When an achievement needs `URequirement_Persisted` data that the stats system does not track (e.g. a per-achievement counter of events under specific conditions):

```cpp
// In game module bridge component on APlayerState, server only:
void UPirateAchievementBridge::OnSpecialEventOccurred()
{
    UAchievementComponent* AC = PlayerState->FindComponentByClass<UAchievementComponent>();
    if (!AC) return;

    FRequirementPayload Payload;
    Payload.Counters.Add(TAG_AchievementCounter_StormKills, ++StormKillCount);
    AC->InjectRequirementPayload(TAG_Achievement_StormReaper, Payload);
}
```

GameCore stores this payload and injects it into the `ContextBuilder` when the watcher evaluates `AdditionalRequirements` for `TAG_Achievement_StormReaper`.

### 5. Reacting to an achievement unlock (game module)

```cpp
void URewardBridge::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        Handle = Bus->StartListening<FAchievementUnlockedEvent>(
            TAG_GameCoreEvent_Achievement_Unlocked, this,
            [this](FGameplayTag, const FAchievementUnlockedEvent& Event)
            {
                GrantRewardForAchievement(Event.PlayerId, Event.AchievementTag);
            });
    }
}
```
