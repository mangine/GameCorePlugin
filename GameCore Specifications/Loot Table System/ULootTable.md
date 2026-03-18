# ULootTable

**File:** `LootTable/ULootTable.h`

```cpp
UCLASS(BlueprintType)
class GAMECORE_API ULootTable : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:

    // Controls the entry selection algorithm.
    // Only Threshold is currently implemented.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table")
    ELootTableType TableType = ELootTableType::Threshold;

    // Entries must be sorted ascending by RollThreshold before use.
    // Sorting is enforced at asset save time.
    // Entries above RollThreshold 1.0 are luck-gated and unreachable at base luck.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table", meta = (TitleProperty = "RollThreshold"))
    TArray<FLootTableEntry> Entries;

    // Number of times this table is rolled per invocation.
    // FInt32Range(1, 1) = always roll exactly once.
    // FInt32Range(1, 3) = roll between 1 and 3 times, each roll independent.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table")
    FInt32Range RollCount = FInt32Range(1);
};
```

---

## Notes

- `RollCount` rolls are **independent**. Each roll runs the full entry selection process.
- Entries are evaluated in ascending threshold order; the highest qualifying entry wins per roll.
- `ULootTable` is a `UPrimaryDataAsset` — registered with the Asset Manager by type `"LootTable"` in `DefaultGame.ini`.
- Entries with `RollThreshold > 1.0` exist purely for luck-gated rewards. They are valid and intentional; they produce no reward at base luck and become reachable as `LuckBonus` increases.
