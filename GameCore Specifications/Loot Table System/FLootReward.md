# FLootReward

Output-only struct. Returned in `TArray<FLootReward>` by `ULootRollerSubsystem::RunLootTable`. Carries the fully resolved reward for the fulfillment layer. Never authored directly by designers — see `FLootEntryReward` for the authoring counterpart embedded in `FLootTableEntry`.

The loot system never fulfills rewards — callers dispatch to the appropriate game system.

**File:** `LootTable/FLootReward.h`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FLootReward
{
    GENERATED_BODY()

    // Drives fulfillment routing in the game layer.
    // Copied from FLootEntryReward::RewardType on the selected entry.
    // Examples:
    //   GameCore.Reward.Item       → add to inventory
    //   GameCore.Reward.Currency   → credit currency system
    //   GameCore.Reward.XP         → call UProgressionSubsystem::GrantXP
    //   GameCore.Reward.Ability    → grant ability to ASC
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag RewardType;

    // The concrete reward definition asset.
    // Copied from FLootEntryReward::RewardDefinition on the selected entry.
    // Null for tag-only rewards where RewardType alone is sufficient for routing.
    // Loaded async by the fulfillment layer — never loaded by the loot system.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UObject> RewardDefinition;

    // Resolved quantity. Always >= 1.
    // Resolved by the roller from FLootTableEntry::Quantity range
    // using FLootTableEntry::QuantityDistribution.
    UPROPERTY(BlueprintReadOnly)
    int32 Quantity = 1;

    bool IsValid() const { return RewardType.IsValid(); }
};
```

---

## Fulfillment Routing Pattern

The game layer switches on `RewardType` to dispatch. Assets are loaded async before casting:

```cpp
void UMyRewardHandler::FulfillRewards(
    APlayerState*              Recipient,
    const TArray<FLootReward>& Rewards,
    const FLootRollContext&    Context)
{
    for (const FLootReward& Reward : Rewards)
    {
        if (Reward.RewardType.MatchesTag(TAG_Reward_Item))
        {
            // Soft ref — load async before use in production.
            // Shown synchronous here for brevity.
            UItemDefinition* Def =
                Cast<UItemDefinition>(Reward.RewardDefinition.LoadSynchronous());
            if (Def) InventorySystem->AddItem(Recipient, Def, Reward.Quantity);
        }
        else if (Reward.RewardType.MatchesTag(TAG_Reward_XP))
        {
            UXPRewardDefinition* Def =
                Cast<UXPRewardDefinition>(Reward.RewardDefinition.LoadSynchronous());
            if (Def)
                ProgressionSubsystem->GrantXP(
                    Recipient, Def->ProgressionTag, Reward.Quantity, ...);
        }
        else if (Reward.RewardType.MatchesTag(TAG_Reward_Currency))
        {
            UCurrencyDefinition* Def =
                Cast<UCurrencyDefinition>(Reward.RewardDefinition.LoadSynchronous());
            if (Def) CurrencySystem->Credit(Recipient, Def, Reward.Quantity);
        }
        // Extend per game without touching GameCore.
    }
}
```

`FLootReward` is intentionally dumb — it carries data, not behavior. All routing and loading lives in the game layer.

---

## Gameplay Tags

```
GameCore.Reward.Item
GameCore.Reward.Currency
GameCore.Reward.XP
GameCore.Reward.Ability
```

Game-specific reward types extend this hierarchy in the game module's tag file.
