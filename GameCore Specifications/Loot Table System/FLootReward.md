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

    // Final resolved quantity after applying EQuantityDistribution to the entry range.
    // Always >= 1 for valid rewards.
    UPROPERTY(BlueprintReadOnly)
    int32 Quantity = 1;

    bool IsValid() const { return RewardType.IsValid(); }
};
```

---

## Asset Reference — FLootTableEntry

`FLootReward` itself carries no asset reference — it is the **output** of a roll and holds only resolved data. The asset reference lives on `FLootTableEntry::RewardDefinition`, which is where the editor picker filtering is applied:

```cpp
// In FLootTableEntry — this is where ILootRewardable filtering is enforced.
// The meta tag signals FFLootTableEntryCustomization to replace the default
// picker with an ILootRewardable-filtered SObjectPropertyEntryBox.
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward",
    meta = (GameCoreInterfaceFilter = "LootRewardable"))
TSoftObjectPtr<UObject> RewardDefinition;
```

At roll time, `RewardDefinition` is copied from the selected `FLootTableEntry` into `FLootReward` as a resolved soft reference. The loot system never loads it — that is the fulfillment layer's responsibility.

See [ILootRewardable](ILootRewardable.md) for the full filtering mechanism and [FLootTableEntry](FLootTableEntry.md) for the full entry definition.

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
            // RewardDefinition is a soft ref — load async before use in production.
            // Shown here as synchronous for brevity.
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
