# UStatDefinition

**Module:** `GameCore` | **File:** `Stats/StatDefinition.h`

---

## Role

DataAsset that fully defines one tracked stat. Authored in the editor, referenced by `UStatComponent`. Contains the stat's identity (`StatTag`), its increment rules, and an optional requirement gate. Carries no runtime state.

---

## Class Definition

```cpp
// GameCore/Source/GameCore/Stats/StatDefinition.h

UCLASS()
class GAMECORE_API UStatDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    // Primary key. Must be unique across all UStatDefinition assets.
    UPROPERTY(EditDefaultsOnly, Category="Stat")
    FGameplayTag StatTag;

    // One or more rules that drive this stat's value.
    // Each rule listens to an Event Bus channel and extracts a float increment.
    // Multiple rules are additive — all fire independently.
    UPROPERTY(EditDefaultsOnly, Instanced, Category="Stat")
    TArray<TObjectPtr<UStatIncrementRule>> Rules;

    // Optional. If set, all rules on this definition are gated by this requirement set.
    // Requirements are evaluated at increment time on the server.
    // If unmet, the increment is silently discarded.
    UPROPERTY(EditDefaultsOnly, Category="Stat")
    TObjectPtr<URequirementSet> TrackingRequirements;

    // Achievements that must be re-evaluated when this stat changes.
    // Soft refs: prevent a hard module dependency from Stats onto Achievement.
    // Resolved by UAchievementComponent at BeginPlay.
    // Populated by content authors.
    UPROPERTY(EditDefaultsOnly, Category="Achievements")
    TArray<TSoftObjectPtr<UAchievementDefinition>> AffectedAchievements;
};
```

---

## Notes

- `Rules` uses `Instanced` so each rule object is owned by the DataAsset and editable inline in the Details panel — no separate asset per rule.
- `TrackingRequirements` is evaluated at increment time, not at registration time. Requirements can reference the instigating player's state.
- A `StatTag` collision between two `UStatDefinition` assets is a logic error, validated in non-shipping builds by `UStatComponent::ValidateDefinitions()`.
- `AffectedAchievements` uses soft object pointers (`TSoftObjectPtr`) to avoid a compile-time dependency from the Stats module onto the Achievement module.
- The asset itself holds no runtime state — all mutable values live in `UStatComponent`.

---

## Example Assets

```
DA_Stat_EnemiesKilled
  StatTag:               Stat.Player.EnemiesKilled
  Rules:
    [0] UConstantIncrementRule
          ChannelTag: GameplayMessage.Combat.EnemyKilled
          Amount:     1.0
  TrackingRequirements:  (none)
  AffectedAchievements:  [DA_Achievement_KillEnemy100, DA_Achievement_WarriorOfTheSeas]

DA_Stat_TotalDamageDealt
  StatTag:               Stat.Player.TotalDamageDealt
  Rules:
    [0] UDamageIncrementRule          // game module subclass
          ChannelTag: GameplayMessage.Combat.DamageDealt
  TrackingRequirements:  RS_PlayerAlive
  AffectedAchievements:  [DA_Achievement_WarriorOfTheSeas]
```
