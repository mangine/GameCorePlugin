# ULootTable

`UPrimaryDataAsset`. The authored loot table asset. Designers create these in the Content Browser and configure entries, roll count, and table type.

**File:** `LootTable/ULootTable.h`

---

## Declaration

```cpp
UCLASS(BlueprintType)
class GAMECORE_API ULootTable : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:

    // Controls the entry selection algorithm.
    // Only ELootTableType::Threshold is currently implemented.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table")
    ELootTableType TableType = ELootTableType::Threshold;

    // Entries sorted ascending by RollThreshold before use.
    // Use the "Sort Entries" button in the Details panel to sort.
    // IsDataValid() auto-sorts on save and errors on duplicate thresholds.
    // Entries with RollThreshold > 1.0 are luck-gated and unreachable at base luck.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table",
        meta = (TitleProperty = "RollThreshold"))
    TArray<FLootTableEntry> Entries;

    // Number of times this table is rolled per invocation.
    // FInt32Range(1)    = always roll exactly once.
    // FInt32Range(1, 3) = roll between 1 and 3 times, each roll independent.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table")
    FInt32Range RollCount = FInt32Range(1);

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
```

---

## `IsDataValid` Implementation

```cpp
EDataValidationResult ULootTable::IsDataValid(FDataValidationContext& Context) const
{
    // Auto-sort: self-healing, no error emitted for sort order violations.
    // Note: const_cast is required because UE's IsDataValid signature is const.
    // The sort is a safe mutation — sort order is not meaningful to the saved asset state.
    const_cast<ULootTable*>(this)->Entries.Sort(
        [](const FLootTableEntry& A, const FLootTableEntry& B)
        { return A.RollThreshold < B.RollThreshold; });

    // Duplicate threshold check — errors, no auto-fix.
    EDataValidationResult Result = EDataValidationResult::Valid;
    for (int32 i = 1; i < Entries.Num(); ++i)
    {
        if (FMath::IsNearlyEqual(
            Entries[i].RollThreshold, Entries[i-1].RollThreshold, KINDA_SMALL_NUMBER))
        {
            Context.AddError(FText::Format(
                LOCTEXT("DuplicateThreshold",
                    "Entries {0} and {1} share RollThreshold {2}. "
                    "Duplicate thresholds cause non-deterministic reward selection."),
                i-1, i, Entries[i].RollThreshold));
            Result = EDataValidationResult::Invalid;
        }
    }
    return Result;
}
```

---

## Editor Tooling

### Sort Button — `FULootTableCustomization`

`IDetailCustomization` registered in `GameCoreEditor` for `ULootTable`.

**File:** `GameCoreEditor/LootTable/FULootTableCustomization.h`

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

On click:
1. Sorts `Entries` ascending by `RollThreshold` in-place.
2. Marks the asset dirty.
3. Forces a Details panel refresh via `DetailBuilder->ForceRefreshDetails()`.

Registration (in `FGameCoreEditorModule::StartupModule()`):
```cpp
PropertyModule.RegisterCustomClassLayout(
    ULootTable::StaticClass()->GetFName(),
    FOnGetDetailCustomizationInstance::CreateStatic(
        &FULootTableCustomization::MakeInstance));
```

---

## Asset Manager Registration

`ULootTable` is a `UPrimaryDataAsset`. Add to `DefaultGame.ini`:

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(PrimaryAssetType="LootTable",AssetBaseClass=/Script/GameCore.LootTable,bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/LootTables")),Rules=(Priority=0,bApplyRecursively=True))
```

Adjust `Directories` to match the project content layout. The type name `"LootTable"` must match `ULootTable::GetPrimaryAssetId()` implementation.

---

## Notes

- `RollCount` rolls are **independent** — each roll runs the full entry selection and downgrade logic separately.
- Entries are scanned in descending threshold order during selection to find the highest qualifying candidate.
- `ULootTable` does not enforce that entries are sorted at runtime — the roller assumes sorted order. `IsDataValid` auto-sorts on save; the sort button provides an on-demand fix during editing.
