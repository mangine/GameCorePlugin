# FLootReward

Output-only struct. Returned in `TArray<FLootReward>` by `ULootRollerSubsystem::RunLootTable`. Carries the fully resolved reward for the fulfillment layer. Never authored by designers — see `FLootEntryReward` (inside `FLootTableEntry.md`) for the authoring counterpart.

**File:** `LootTable/FLootReward.h`

---

## Declaration

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

    // Resolved quantity. Always >= 1 for valid rewards.
    // Resolved by the roller from FLootTableEntry::Quantity using
    // FLootTableEntry::QuantityDistribution.
    UPROPERTY(BlueprintReadOnly)
    int32 Quantity = 1;

    // Returns true if RewardType is valid. A reward with an invalid tag is a no-op.
    bool IsValid() const { return RewardType.IsValid(); }
};
```

---

## Notes

- `FLootReward` is data-only. No behavior, no routing logic, no system references.
- `RewardDefinition` may be null for tag-only rewards (e.g. `GameCore.Reward.XP` with quantity encoding the amount).
- The fulfillment layer must cast `RewardDefinition` to the expected type after async load. The loot system does not validate the cast.
- `IsValid()` checks `RewardType` only. A reward with a valid tag and a null `RewardDefinition` is still considered valid — the fulfillment layer decides how to handle it.
