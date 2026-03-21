# UAchievementDefinition

**Module:** `GameCore` | **File:** `Stats/Achievements/AchievementDefinition.h`

---

## Role

DataAsset that fully defines one achievement. Authored in the editor, referenced by `UAchievementComponent`. Contains the achievement's identity, its stat thresholds, and an optional `URequirementList` for non-stat conditions. Carries no runtime state.

---

## Class Definition

```cpp
// GameCore/Source/GameCore/Stats/Achievements/AchievementDefinition.h

UCLASS(BlueprintType)
class GAMECORE_API UAchievementDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    // Primary key. Must be unique across all UAchievementDefinition assets.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Achievement")
    FGameplayTag AchievementTag;

    // All thresholds must pass (AND semantics). Minimum one entry required.
    UPROPERTY(EditDefaultsOnly, Category="Achievement")
    TArray<FStatThreshold> StatThresholds;

    // Optional. Evaluated via the Requirement System watcher.
    // Null = no additional gating beyond stat thresholds.
    // Must reference requirements with ServerOnly authority.
    UPROPERTY(EditDefaultsOnly, Category="Achievement")
    TObjectPtr<URequirementList> AdditionalRequirements;
};
```

---

## Notes

- `StatThresholds` must not be empty. Validated at `UAchievementComponent::BeginPlay` in non-shipping builds.
- `AdditionalRequirements` authority must be `ServerOnly`. Achievements are always server-authoritative.
- A single `UAchievementDefinition` asset can be referenced by multiple `UStatDefinition.AffectedAchievements` arrays (multi-stat achievements require this).
- The asset carries no runtime state. All mutable state lives in `UAchievementComponent`.
- `AchievementTag` collision is a logic error validated at `UAchievementComponent::BeginPlay` in non-shipping builds.

---

## Example Assets

```
DA_Achievement_KillEnemy100
  AchievementTag:  Achievement.Combat.KillEnemy100
  StatThresholds:
    [0] StatTag: Stat.Player.EnemiesKilled   Threshold: 100
  AdditionalRequirements: (none)

DA_Achievement_WarriorOfTheSeas
  AchievementTag:  Achievement.Combat.WarriorOfTheSeas
  StatThresholds:
    [0] StatTag: Stat.Player.EnemiesKilled     Threshold: 500
    [1] StatTag: Stat.Player.TotalDamageDealt  Threshold: 100000
  AdditionalRequirements: (none)

DA_Achievement_NightPirate
  AchievementTag:  Achievement.World.NightPirate
  StatThresholds:
    [0] StatTag: Stat.Player.EnemiesKilledAtNight  Threshold: 50
  AdditionalRequirements: RL_IsNightTime
```
