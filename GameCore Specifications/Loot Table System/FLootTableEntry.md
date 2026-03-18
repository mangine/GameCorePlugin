# FLootTableEntry

**File:** `LootTable/FLootTableEntry.h`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FLootTableEntry
{
    GENERATED_BODY()

    // ── Selection ────────────────────────────────────────────────────────────

    // Threshold in [0.0, ∞). Entries are sorted ascending by this value.
    // For Threshold mode: entry wins when rolled value >= this threshold,
    // and no higher-threshold entry also qualifies.
    // Values above 1.0 are luck-gated: only reachable when LuckBonus > (threshold - 1.0).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ClampMin = "0.0"))
    float RollThreshold = 0.0f;

    // ── Conditions ───────────────────────────────────────────────────────────

    // Optional per-entry gate evaluated against the roll instigator.
    // All requirements must pass (AND). See selection algorithm for failure behaviour.
    // Must contain only synchronous requirements — validated at cook time.
    // For OR/NOT logic, use URequirement_Composite as a single array element.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Entry")
    TArray<TObjectPtr<URequirement>> EntryRequirements;

    // Controls what happens when this entry is selected but EntryRequirements fail.
    // true  — downgrade to the next lower qualifying entry that passes requirements.
    // false — no reward is granted for this roll (default).
    // Applies only to this entry; each entry has its own flag.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
    bool bDowngradeOnRequirementFailed = false;

    // ── Reward Payload ───────────────────────────────────────────────────────

    // Authored reward data. Contains RewardType tag and RewardDefinition asset.
    // Mutually exclusive with NestedTable. If both are set, NestedTable takes priority
    // and Reward is ignored. An ensure() fires in non-shipping builds.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward",
        meta = (EditCondition = "!NestedTable"))
    FLootEntryReward Reward;

    // If set, rolling this entry recurses into the nested table instead of
    // producing a direct reward. Counts as one recursion depth increment.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
    TSoftObjectPtr<ULootTable> NestedTable;

    // ── Quantity ─────────────────────────────────────────────────────────────

    // Quantity range for this entry. The roller samples this range using
    // QuantityDistribution and writes the result into FLootReward::Quantity on output.
    // Ignored when NestedTable is set — quantity comes from the sub-table's entries.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quantity")
    FInt32Range Quantity = FInt32Range(1);

    // Distribution curve for quantity rolls within the range.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quantity")
    EQuantityDistribution QuantityDistribution = EQuantityDistribution::Uniform;
};
```

---

## `FLootEntryReward`

Authoring-only struct embedded in `FLootTableEntry::Reward`. Holds the data designers configure per entry. Separate from `FLootReward` (the output struct) to keep authoring and output roles cleanly distinct.

**File:** `LootTable/FLootEntryReward.h`

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
    // Null for tag-only rewards where RewardType alone is sufficient.
    UPROPERTY(EditAnywhere, BlueprintReadOnly,
        meta = (GameCoreInterfaceFilter = "LootRewardable"))
    TSoftObjectPtr<UObject> RewardDefinition;
};
```

At roll time, `FLootEntryReward` fields are copied into the output `FLootReward` alongside the resolved `Quantity`.

---

## `EQuantityDistribution`

```cpp
UENUM(BlueprintType)
enum class EQuantityDistribution : uint8
{
    // Flat random: every value in [Min, Max] equally likely.
    Uniform,

    // Bell curve: values near the midpoint of [Min, Max] are more likely.
    // Implemented as average of two uniform rolls (triangular distribution).
    // No external library dependency.
    Normal,
};
```

---

## Notes

- `EntryRequirements` uses the same `URequirement` pattern as the Interaction and Quest systems. Synchronous-only — the roller never suspends.
- `bDowngradeOnRequirementFailed` triggers a linear downgrade walk: after the highest qualifying entry fails requirements, the roller walks down to the next lower entry and retries. Each candidate entry's own `bDowngradeOnRequirementFailed` is checked — downgrade chains only continue while consecutive entries have the flag set.
- `NestedTable` is a soft reference loaded synchronously via `LoadSynchronous()`. Tables referenced from loaded actors (chests, mobs) are already in memory — sync load is a cache hit. Pre-load explicitly only for tables referenced via soft pointers on actors that load independently.
- A single entry **cannot** be both a leaf reward and a nested table. `ensure(!Reward.RewardType.IsValid() || NestedTable.IsNull())` fires in non-shipping builds if both are configured.
- See [FLootReward](FLootReward.md) for the output struct and [ILootRewardable](ILootRewardable.md) for `RewardDefinition` editor picker filtering.
