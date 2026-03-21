# Achievement System — Usage

---

## Setup

1. Add `UAchievementComponent` to your `APlayerState` Blueprint.
2. Ensure `UStatComponent` is also present (required — achievements read stat values from it).
3. For achievements with `AdditionalRequirements`, also add `URequirementWatcherComponent` to `APlayerState`.

---

## 1 — Single-stat achievement (most common)

No C++ required.

1. Create `DA_Achievement_KillEnemy100` (`UAchievementDefinition`):
   - `AchievementTag` = `Achievement.Combat.KillEnemy100`
   - `StatThresholds[0]`: `StatTag` = `Stat.Player.EnemiesKilled`, `Threshold` = `100`
   - `AdditionalRequirements` = none
2. Open `DA_Stat_EnemiesKilled`. Add `DA_Achievement_KillEnemy100` to `AffectedAchievements`.
3. Add `DA_Achievement_KillEnemy100` to `UAchievementComponent.Definitions` on the `APlayerState` Blueprint.

Done.

---

## 2 — Multi-stat achievement

```
DA_Achievement_WarriorOfTheSeas
  AchievementTag:  Achievement.Combat.WarriorOfTheSeas
  StatThresholds:
    [0] StatTag: Stat.Player.EnemiesKilled   Threshold: 500
    [1] StatTag: Stat.Player.TotalDamageDealt  Threshold: 100000
  AdditionalRequirements: (none)
```

Add `DA_Achievement_WarriorOfTheSeas` to `AffectedAchievements` on **both** `DA_Stat_EnemiesKilled` and `DA_Stat_TotalDamageDealt`. Evaluation fires when either stat changes, but all thresholds must pass before grant.

---

## 3 — Achievement with additional requirements

```
DA_Achievement_NightPirate
  AchievementTag:  Achievement.World.NightPirate
  StatThresholds:
    [0] StatTag: Stat.Player.EnemiesKilledAtNight  Threshold: 50
  AdditionalRequirements: RL_IsNightTime
```

Ensure `URequirementWatcherComponent` is present on `APlayerState`. The watcher on `RL_IsNightTime` is registered at `BeginPlay`. The achievement grants only when both the stat threshold and the requirement list pass simultaneously.

---

## 4 — Achievement requiring persisted requirement data

For non-stat conditions that need persistence (e.g. a per-achievement counter under specific conditions):

```cpp
// Game module bridge component on APlayerState, server only:
void UPirateAchievementBridge::OnStormKill()
{
    UAchievementComponent* AC = PlayerState->FindComponentByClass<UAchievementComponent>();
    if (!AC) return;

    FRequirementPayload Payload;
    Payload.Counters.Add(TAG_AchievementCounter_StormKills, ++StormKillCount);
    AC->InjectRequirementPayload(TAG_Achievement_StormReaper, Payload);
}
```

GameCore stores and injects this payload into the `ContextBuilder` when evaluating `AdditionalRequirements` for `TAG_Achievement_StormReaper`.

---

## 5 — React to an achievement unlock

```cpp
void UMyRewardSystem::Initialize(FSubsystemCollectionBase& Collection)
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

void UMyRewardSystem::Deinitialize()
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
        Bus->StopListening(Handle);
    Super::Deinitialize();
}
```

---

## 6 — Display achievement progress (server-side)

```cpp
// Server only — stat values not replicated by default.
TArray<FStatThresholdProgress> Progress;
if (AchievementComp->GetProgress(TAG_Achievement_KillEnemy100, Progress))
{
    for (const FStatThresholdProgress& P : Progress)
    {
        UE_LOG(LogGame, Log, TEXT("%s: %.0f / %.0f (%.0f%%)"),
            *P.StatTag.ToString(), P.Current, P.Required,
            P.GetNormalized() * 100.f);
    }
}
```

For client-side progress display, replicate the required stat values via `APlayerState` — `RuntimeValues` is not replicated by this system.

---

## 7 — Check if earned (client-safe)

```cpp
// Reads the replicated EarnedAchievements container — safe on client.
bool bEarned = AchievementComp->HasAchievement(TAG_Achievement_KillEnemy100);
```

---

## Authoring Checklist

1. Create `UAchievementDefinition` DataAsset. Set `AchievementTag`, `StatThresholds`, optionally `AdditionalRequirements`.
2. For each `StatTag` in `StatThresholds`, open the corresponding `UStatDefinition` and add the new asset to `AffectedAchievements`.
3. Add the `UAchievementDefinition` to `UAchievementComponent.Definitions` on the `APlayerState` Blueprint.
4. If `AdditionalRequirements` is set, ensure `URequirementWatcherComponent` is on `APlayerState`.
5. If `URequirement_Persisted` subclasses are used, implement a game module bridge calling `InjectRequirementPayload`.
6. *(Optional)* Subscribe to `GameCoreEvent.Achievement.Unlocked` for rewards or UI.
