// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LootTable/FLootTableEntry.h"
#include "ULootTable.generated.h"

/**
 * ULootTable
 *
 * UPrimaryDataAsset. The authored loot table asset. Designers create these in
 * the Content Browser and configure entries, roll count, and table type.
 *
 * Asset Manager primary asset type: "LootTable"
 * Register in DefaultGame.ini under [/Script/Engine.AssetManagerSettings].
 */
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

    // Returns PrimaryAssetId with type "LootTable" and name from the asset name.
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(FPrimaryAssetType(TEXT("LootTable")), GetFName());
    }

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
