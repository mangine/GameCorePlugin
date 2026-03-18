# FLootReward

The reward contract produced by the loot roller. Carries everything the fulfillment layer needs to grant the reward. The loot system never fulfills rewards — callers dispatch to the appropriate game system.

**File:** `LootTable/FLootReward.h`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FLootReward
{
    GENERATED_BODY()

    // Drives fulfillment routing in the game layer.
    // Examples:
    //   GameCore.Reward.Item       → add to inventory
    //   GameCore.Reward.Currency   → credit currency system
    //   GameCore.Reward.XP         → call UProgressionSubsystem::GrantXP
    //   GameCore.Reward.Ability    → grant ability to ASC
    // The loot system does not validate or act on this tag.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag RewardType;

    // The concrete reward definition asset (item def, currency def, XP config, etc.).
    // Null for tag-only rewards where RewardType alone is sufficient for routing.
    // Loaded async by the fulfillment layer — never loaded by the loot system.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UPrimaryDataAsset> RewardDefinition;

    // Final resolved quantity after applying EQuantityDistribution to the entry range.
    // Always >= 1 for valid rewards.
    UPROPERTY(BlueprintReadOnly)
    int32 Quantity = 1;

    bool IsValid() const { return RewardType.IsValid(); }
};
```

---

## Fulfillment Routing Pattern

The game layer switches on `RewardType` to dispatch:

```cpp
void UMyRewardHandler::FulfillRewards(
    APlayerState* Recipient,
    const TArray<FLootReward>& Rewards,
    const FLootRollContext& Context)
{
    for (const FLootReward& Reward : Rewards)
    {
        if (Reward.RewardType.MatchesTag(TAG_Reward_Item))
            InventorySystem->AddItem(Recipient, Reward.RewardDefinition, Reward.Quantity);
        else if (Reward.RewardType.MatchesTag(TAG_Reward_XP))
            ProgressionSubsystem->GrantXP(Recipient, /* resolve tag from RewardDefinition */, Reward.Quantity, ...);
        else if (Reward.RewardType.MatchesTag(TAG_Reward_Currency))
            CurrencySystem->Credit(Recipient, Reward.RewardDefinition, Reward.Quantity);
        // ... extend per game without touching GameCore
    }
}
```

`FLootReward` is intentionally dumb — it carries data, not behavior. All routing lives in the game layer.

---

## Gameplay Tags

```
GameCore.Reward.Item
GameCore.Reward.Currency
GameCore.Reward.XP
GameCore.Reward.Ability
```

Game-specific reward types extend this hierarchy in the game module's tag file.
