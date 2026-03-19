# UStatDefinition

## Role

Data asset that fully defines one tracked stat. Authored in the editor, referenced by `UStatComponent`. Contains the stat's identity, its increment rules, and an optional requirement gate.

---

## Class Definition

```cpp
// GameCore module
UCLASS()
class GAMECORE_API UStatDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    // Primary key. Must be unique across all UStatDefinition assets in the project.
    UPROPERTY(EditDefaultsOnly, Category="Stat")
    FGameplayTag StatTag;

    // One or more rules that drive this stat's value.
    // Each rule listens to a GMS channel and produces a float increment.
    // Multiple rules are additive — all fire independently.
    UPROPERTY(EditDefaultsOnly, Instanced, Category="Stat")
    TArray<TObjectPtr<UStatIncrementRule>> Rules;

    // Optional. If set, all rules on this definition are gated by this requirement set.
    // If requirements are not met at increment time, the increment is silently discarded.
    UPROPERTY(EditDefaultsOnly, Category="Stat")
    TObjectPtr<URequirementSet> TrackingRequirements;
};
```

---

## Notes

- `Rules` uses `Instanced` so each rule object is owned by the DataAsset and editable inline in the Details panel — no separate asset per rule.
- `TrackingRequirements` is evaluated at increment time on the server, not at registration time. Requirements can reference the instigating player context.
- A `StatTag` collision between two `UStatDefinition` assets is a logic error. Validated in non-shipping builds at `UStatComponent::BeginPlay`.
- The DataAsset itself holds no runtime state — all mutable values live in `UStatComponent`.

---

## Example Asset: DA_Stat_EnemiesKilled

```
StatTag:               Stat.Player.EnemiesKilled
Rules:
  [0] UConstantIncrementRule
      ChannelTag: GameplayMessage.Combat.EnemyKilled
      Amount:     1.0
TrackingRequirements:  (none)
```

## Example Asset: DA_Stat_TotalDamageDealt

```
StatTag:               Stat.Player.TotalDamageDealt
Rules:
  [0] UDamageIncrementRule          // game module subclass
      ChannelTag: GameplayMessage.Combat.DamageDealt
TrackingRequirements:  RS_PlayerAlive
```
