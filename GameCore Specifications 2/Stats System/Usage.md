# Stats System — Usage

---

## Setup

`UStatComponent` must be added to your `APlayerState` Blueprint. Add `UStatDefinition` DataAssets to its `Definitions` array. That is the only setup required.

If any definition uses a `URequirementSet`, ensure the relevant requirement assets are authored and assigned.

For achievements driven by stats, also add `UAchievementComponent` and configure it — see the [Achievement System Usage](./Achievement%20System/Usage.md).

---

## 1 — Stat that increments by 1 on an Event Bus message

No C++ required.

1. Create a `UStatDefinition` DataAsset, e.g. `DA_Stat_EnemiesKilled`.
2. Set `StatTag` = `Stat.Player.EnemiesKilled`.
3. Add one entry to `Rules`, select `UConstantIncrementRule`.
   - Set `ChannelTag` = `GameplayMessage.Combat.EnemyKilled`.
   - Set `Amount` = `1.0`.
4. Leave `TrackingRequirements` empty.
5. Add `DA_Stat_EnemiesKilled` to `UStatComponent.Definitions` on the `APlayerState` Blueprint.

The component auto-registers the listener at `BeginPlay`. Done.

---

## 2 — Stat that extracts a float from a message payload

Subclass `UStatIncrementRule` in the game module:

```cpp
// Game module — DamageIncrementRule.h
UCLASS(DisplayName="Damage Amount Increment")
class MYGAME_API UDamageIncrementRule : public UStatIncrementRule
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Rule")
    FGameplayTag ChannelTag;

    virtual FGameplayTag GetChannelTag_Implementation() const override { return ChannelTag; }

    virtual float ExtractIncrement_Implementation(const FInstancedStruct& Payload) const override
    {
        if (const FDamageDealtMessage* Msg = Payload.GetPtr<FDamageDealtMessage>())
            return Msg->DamageAmount;
        return 0.f;
    }
};
```

Create `DA_Stat_TotalDamageDealt`, add a `UDamageIncrementRule` entry, set `ChannelTag` = `GameplayMessage.Combat.DamageDealt`. No wiring code needed.

---

## 3 — Gate a stat behind a requirement

On the `UStatDefinition` asset, set `TrackingRequirements` to a `URequirementSet` asset. The stat will not increment while requirements are unmet. The requirement is evaluated on the server at each increment call.

```
DA_Stat_TotalDamageDealt
  TrackingRequirements: RS_PlayerAlive
```

---

## 4 — Manually increment a stat (non-event-driven)

For quest completion rewards, login bonuses, or any non-message-driven increment:

```cpp
// Server only.
if (UStatComponent* Stats = PlayerState->FindComponentByClass<UStatComponent>())
{
    Stats->AddToStat(TAG_Stat_QuestsCompleted, 1.f);
}
```

`AddToStat` performs all the same authority checks, requirement evaluation, dirty-flagging, and Event Bus broadcasting as the event-driven path.

---

## 5 — Read a stat value

```cpp
// Server or client (client returns 0 unless replicated by game module).
float Kills = Stats->GetStat(TAG_Stat_EnemiesKilled);
```

For client-side progress display, replicate a subset of stat values via `APlayerState` — `RuntimeValues` itself is not replicated by this system.

---

## 6 — React to a stat change (downstream system)

Subscribe to `Event.Stat.Changed` on the Event Bus. No direct coupling to `UStatComponent`.

```cpp
void UMySystem::BeginPlay()
{
    Super::BeginPlay();

    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        StatHandle = Bus->StartListening<FStatChangedEvent>(
            TAG_Event_Stat_Changed, this,
            [this](FGameplayTag, const FStatChangedEvent& Event)
            {
                // Filter by StatTag and PlayerId as needed.
                if (Event.StatTag != TAG_Stat_EnemiesKilled) return;
                HandleEnemiesKilledUpdate(Event.PlayerId, Event.NewValue);
            });
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
        Bus->StopListening(StatHandle);
    Super::EndPlay(Reason);
}
```

---

## 7 — Blueprint increment rule

For rapid prototyping, subclass `UStatIncrementRule` in Blueprint:

```
BP_KillStreakIncrementRule
  GetChannelTag → return GameplayMessage.Combat.EnemyKilled
  ExtractIncrement(Payload)
    → call GetKillStreakMessage(Payload)   // game module BP function library helper
    → if valid: return StreakBonus
    → else: return 0.0
```

> `FInstancedStruct::GetPtr<T>()` is not natively exposed to Blueprint. Expose typed helpers in the game module's `UBlueprintFunctionLibrary`:
> 
> ```cpp
> UFUNCTION(BlueprintCallable, BlueprintPure, Category="Stats")
> static const FKillStreakMessage* GetKillStreakMessage(const FInstancedStruct& Payload)
> {
>     return Payload.GetPtr<FKillStreakMessage>();
> }
> ```

Ship builds should use C++ subclasses for performance-critical stats.

---

## 8 — Multi-rule stat

A single stat can be fed by multiple rules listening on different channels. All fire independently; values are additive.

```
DA_Stat_TotalKills
  StatTag: Stat.Player.TotalKills
  Rules:
    [0] UConstantIncrementRule
          ChannelTag: GameplayMessage.Combat.EnemyKilled
          Amount:     1.0
    [1] UConstantIncrementRule
          ChannelTag: GameplayMessage.Combat.BossKilled
          Amount:     5.0    // bosses count extra toward the total
  TrackingRequirements: (none)
```

---

## Gameplay Tag Declarations

```
Stat
  Player
    EnemiesKilled
    TotalDamageDealt
    QuestsCompleted
    ... (game-specific)

Event
  Stat
    Changed
```
