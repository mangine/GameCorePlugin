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
    // Use the "Sort Entries" button in the Details panel to sort manually.
    // IsDataValid() auto-sorts on save and errors on duplicate thresholds.
    // Entries above RollThreshold 1.0 are luck-gated and unreachable at base luck.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table", meta = (TitleProperty = "RollThreshold"))
    TArray<FLootTableEntry> Entries;

    // Number of times this table is rolled per invocation.
    // FInt32Range(1, 1) = always roll exactly once.
    // FInt32Range(1, 3) = roll between 1 and 3 times, each roll independent.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table")
    FInt32Range RollCount = FInt32Range(1);

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
```

---

## Editor Tooling

### Sort Button

Implemented via `IDetailCustomization` on `ULootTable` in the `GameCoreEditor` module.

**File:** `GameCoreEditor/LootTable/ULootTableCustomization.h`

```cpp
class FULootTableCustomization : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    void SortEntries(IDetailLayoutBuilder* DetailBuilder);
};
```

Adds a "Sort Entries" button at the top of the Entries category. On click:
1. Sorts `Entries` ascending by `RollThreshold` in-place.
2. Marks the asset dirty so the change is saved.
3. Forces a Details panel refresh.

Registered in `FGameCoreEditorModule::StartupModule`:

```cpp
PropertyModule.RegisterCustomClassLayout(
    ULootTable::StaticClass()->GetFName(),
    FOnGetDetailCustomizationInstance::CreateStatic(
        &FULootTableCustomization::MakeInstance));
```

### Validation — `IsDataValid`

```cpp
EDataValidationResult ULootTable::IsDataValid(FDataValidationContext& Context) const
{
    // Auto-sort: self-healing, no error emitted for sort order violations.
    // Cast away const — sort is a non-semantic authoring fix, not a state mutation.
    const_cast<ULootTable*>(this)->Entries.Sort(
        [](const FLootTableEntry& A, const FLootTableEntry& B)
        { return A.RollThreshold < B.RollThreshold; });

    // Duplicate threshold check — errors only, no auto-fix.
    // Duplicate thresholds make entry selection non-deterministic.
    EDataValidationResult Result = EDataValidationResult::Valid;
    for (int32 i = 1; i < Entries.Num(); ++i)
    {
        if (FMath::IsNearlyEqual(Entries[i].RollThreshold, Entries[i - 1].RollThreshold, KINDA_SMALL_NUMBER))
        {
            Context.AddError(FText::Format(
                LOCTEXT("DuplicateThreshold",
                    "Entries {0} and {1} share RollThreshold {2}. Duplicate thresholds cause non-deterministic reward selection."),
                i - 1, i, Entries[i].RollThreshold));
            Result = EDataValidationResult::Invalid;
        }
    }
    return Result;
}
```

---

## Notes

- `RollCount` rolls are **independent**. Each roll runs the full entry selection process.
- Entries are evaluated in ascending threshold order; the highest qualifying entry wins per roll.
- `ULootTable` is a `UPrimaryDataAsset` — registered with the Asset Manager by type `"LootTable"` in `DefaultGame.ini`.
- Entries with `RollThreshold > 1.0` exist purely for luck-gated rewards. They are valid and intentional; they produce no reward at base luck and become reachable as `LuckBonus` increases.
