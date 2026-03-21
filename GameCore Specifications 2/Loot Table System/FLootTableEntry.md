# FLootTableEntry and FLootEntryReward

Authoring structs. `FLootTableEntry` is the element type of `ULootTable::Entries`. `FLootEntryReward` is the reward payload embedded in each entry, kept separate from `FLootReward` (the output struct) to maintain a clean authoring vs. output distinction.

**File:** `LootTable/FLootTableEntry.h`

---

## `FLootTableEntry`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FLootTableEntry
{
    GENERATED_BODY()

    // ── Selection ────────────────────────────────────────────────────────────

    // Threshold in [0.0, ∞). Entries sorted ascending by this value.
    // For Threshold mode: entry wins when rolled value >= this threshold,
    // and no higher-threshold entry also qualifies.
    // Values above 1.0 are luck-gated: only reachable when LuckBonus > (threshold - 1.0).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = "0.0"))
    float RollThreshold = 0.0f;

    // ── Conditions ───────────────────────────────────────────────────────────

    // Optional per-entry gate evaluated against the roll instigator.
    // All requirements must pass (AND logic).
    // For OR/NOT logic use URequirement_Composite as a single array element.
    // Must contain only synchronous requirements — the roller never suspends.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Entry")
    TArray<TObjectPtr<URequirement>> EntryRequirements;

    // Controls what happens when this entry is selected but EntryRequirements fail.
    // true  — downgrade to the next lower qualifying entry that passes requirements.
    // false — no reward is granted for this roll (default).
    // Each entry has its own flag. A downgrade chain continues only while
    // consecutive downgraded entries also have this flag set.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
    bool bDowngradeOnRequirementFailed = false;

    // ── Reward Payload ───────────────────────────────────────────────────────

    // Authored reward data. Mutually exclusive with NestedTable.
    // If both are set, NestedTable takes priority and Reward is ignored.
    // An ensure() fires in non-shipping builds if both are configured.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward",
        meta = (EditCondition = "!NestedTable"))
    FLootEntryReward Reward;

    // If set, rolling this entry recurses into the nested table instead of
    // producing a direct reward. Counts as one recursion depth increment.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
    TSoftObjectPtr<ULootTable> NestedTable;

    // ── Quantity ─────────────────────────────────────────────────────────────

    // Quantity range. The roller samples this using QuantityDistribution
    // and writes the result into FLootReward::Quantity on output.
    // Ignored when NestedTable is set — quantity comes from sub-table entries.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quantity")
    FInt32Range Quantity = FInt32Range(1);

    // Distribution curve for quantity rolls within the range.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quantity")
    EQuantityDistribution QuantityDistribution = EQuantityDistribution::Uniform;
};
```

---

## `FLootEntryReward`

Authoring-only. Embedded in `FLootTableEntry::Reward`. Holds the data designers configure per entry. Separate from `FLootReward` to keep authoring and output roles unambiguous.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FLootEntryReward
{
    GENERATED_BODY()

    // Drives fulfillment routing. See FLootReward for tag examples.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag RewardType;

    // The concrete reward definition asset. Must implement ILootRewardable.
    // Enforced at authoring time by FFLootEntryRewardCustomization's filtered picker.
    // Not enforced at runtime — fulfillment layer is responsible for safe casting.
    // Null is valid for tag-only rewards where RewardType alone is sufficient.
    UPROPERTY(EditAnywhere, BlueprintReadOnly,
        meta = (GameCoreInterfaceFilter = "LootRewardable"))
    TSoftObjectPtr<UObject> RewardDefinition;
};
```

At roll time, `FLootEntryReward` fields are copied into the output `FLootReward` alongside the resolved `Quantity`.

---

## Notes

- A single entry **cannot** be both a leaf reward and a nested table. `ensure(!Reward.RewardType.IsValid() || NestedTable.IsNull())` fires in non-shipping builds if both are configured. At runtime in shipping builds `NestedTable` takes priority.
- `EntryRequirements` uses `URequirement` from the Requirement System — the same pattern as Interaction and Quest systems. Synchronous-only.
- `bDowngradeOnRequirementFailed` triggers a linear downgrade walk: the roller walks downward through consecutive entries, stopping at the first entry that either passes requirements or has `bDowngradeOnRequirementFailed = false`.
- `NestedTable` is a soft reference loaded via `LoadSynchronous()`. This is safe when the nested table's actor is already in memory. Explicitly pre-load nested table assets that are referenced via soft pointers on independently loaded actors.
