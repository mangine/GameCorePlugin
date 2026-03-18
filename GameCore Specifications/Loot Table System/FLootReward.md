# FLootReward

The reward contract. Used in two roles:
- **Authoring** — embedded inside `FLootTableEntry::Reward`. Designers fill in `RewardType` and `RewardDefinition` in the entry asset. `Quantity` is not authored here; it lives on `FLootTableEntry` and is resolved at roll time.
- **Output** — returned in `TArray<FLootReward>` by `ULootRollerSubsystem::RunLootTable`. Carries the fully resolved `RewardType`, `RewardDefinition`, and `Quantity` for the fulfillment layer to act on.

The loot system never fulfills rewards — callers dispatch to the appropriate game system.

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
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag RewardType;

    // The concrete reward definition asset (item def, currency def, XP config, etc.).
    // Must implement ILootRewardable — enforced at authoring time by the editor asset
    // picker filter in FFLootTableEntryCustomization. Not enforced at runtime.
    // Null for tag-only rewards where RewardType alone is sufficient for routing
    // (e.g. a reward that grants a fixed scripted effect with no associated data asset).
    // Loaded async by the fulfillment layer — never loaded by the loot system.
    //
    // The meta tag causes FFLootTableEntryCustomization to replace the default picker
    // with an ILootRewardable-filtered SObjectPropertyEntryBox.
    UPROPERTY(EditAnywhere, BlueprintReadOnly,
        meta = (GameCoreInterfaceFilter = "LootRewardable"))
    TSoftObjectPtr<UObject> RewardDefinition;

    // Resolved quantity. Not authored here — set by the roller from FLootTableEntry
    // Quantity range after applying EQuantityDistribution. Always >= 1 on output.
    // Zero in the authoring context (inside FLootTableEntry) — ignored there.
    UPROPERTY(BlueprintReadOnly)
    int32 Quantity = 0;

    bool IsValid() const { return RewardType.IsValid(); }
};
```

---

## Authoring vs Output

| Field | Authored in entry? | Set on output? |
|---|---|---|
| `RewardType` | Yes — designer picks the routing tag | Yes — copied from entry |
| `RewardDefinition` | Yes — designer picks the asset via filtered picker | Yes — copied from entry |
| `Quantity` | No — lives on `FLootTableEntry.Quantity` range | Yes — resolved by roller from the range |

`Quantity` being zero in the authored struct is intentional and harmless — it is never read from `FLootTableEntry::Reward.Quantity`. The roller always overwrites it from `FLootTableEntry::Quantity` when building output.

---

## Editor Picker Filtering

`RewardDefinition` carries `meta = (GameCoreInterfaceFilter = "LootRewardable")`. `FFLootTableEntryCustomization` reads this tag and replaces the default `UObject` picker with an `SObjectPropertyEntryBox` filtered to assets implementing `ILootRewardable`. See [ILootRewardable](ILootRewardable.md) for the full mechanism.

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
