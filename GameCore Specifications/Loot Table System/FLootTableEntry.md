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

    // Optional per-entry gate evaluated against Context.Instigator before selection.
    // All requirements must pass (AND). Failed entries are skipped this roll.
    // Must contain only synchronous requirements — validated at cook time.
    // For OR/NOT logic, use URequirement_Composite as a single array element.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Entry")
    TArray<TObjectPtr<URequirement>> EntryRequirements;

    // ── Reward Payload ───────────────────────────────────────────────────────

    // Mutually exclusive with NestedTable. If both are set, NestedTable takes priority
    // and Reward is ignored. An ensure() fires in non-shipping builds.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward",
        meta = (EditCondition = "!NestedTable"))
    FLootReward Reward;

    // If set, rolling this entry recurses into the nested table instead of
    // producing a direct reward. Counts as one recursion depth increment.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
    TSoftObjectPtr<ULootTable> NestedTable;

    // ── Quantity ─────────────────────────────────────────────────────────────

    // Quantity awarded when this entry is selected. Applied to FLootReward.Quantity.
    // Ignored when NestedTable is set — quantity comes from the sub-table's entries.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quantity")
    FInt32Range Quantity = FInt32Range(1);

    // Distribution curve for quantity rolls within the range.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quantity")
    EQuantityDistribution QuantityDistribution = EQuantityDistribution::Uniform;
};
```

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

- `EntryRequirements` uses the same `URequirement` pattern as the Interaction System and Quest System. Synchronous-only — the roller never suspends.
- `NestedTable` is a soft reference. The roller loads it synchronously via `LoadSynchronous()` — tables must be small enough that sync load is acceptable on the server. Large tables should be pre-loaded by the caller.
- A single entry **cannot** be both a leaf reward and a nested table. `ensure(!Reward.IsValid() || NestedTable.IsNull())` fires in non-shipping builds if both are configured.
