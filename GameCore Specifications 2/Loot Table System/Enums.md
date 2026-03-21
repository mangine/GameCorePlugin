# Loot Table System — Enums

**File:** `LootTable/FLootTableEntry.h` (defined alongside `FLootTableEntry`)

---

## `ELootTableType`

Controls the entry selection algorithm used by `ULootRollerSubsystem::RollTableInternal`.

```cpp
UENUM(BlueprintType)
enum class ELootTableType : uint8
{
    // Roll a value in [0.0, 1.0 + LuckBonus].
    // Select the entry with the highest RollThreshold that is <= the rolled value.
    // Entries with RollThreshold > 1.0 are luck-gated: unreachable at base luck.
    // This is the only currently implemented selection model.
    Threshold,
};
```

**Only `Threshold` is implemented.** The enum exists to leave the door open for future selection models (e.g. weighted random, sequential) without a breaking API change. Adding a new type requires one enum value and one new code path in `RollTableInternal`.

---

## `EQuantityDistribution`

Controls the probability distribution used when resolving a quantity within `FLootTableEntry::Quantity` range.

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

See `ULootRollerSubsystem::ResolveQuantity` for the implementation.
