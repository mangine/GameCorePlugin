// Copyright GameCore Plugin. All Rights Reserved.
#include "LootTable/ULootTable.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "LootTable"

EDataValidationResult ULootTable::IsDataValid(FDataValidationContext& Context) const
{
    // Auto-sort: self-healing, no error emitted for sort order violations.
    // Note: const_cast is required because UE's IsDataValid signature is const.
    // The sort is a safe mutation — sort order is not meaningful to the saved asset state.
    // See Architecture Known Issues: "IsDataValid mutates const object".
    const_cast<ULootTable*>(this)->Entries.Sort(
        [](const FLootTableEntry& A, const FLootTableEntry& B)
        { return A.RollThreshold < B.RollThreshold; });

    // Duplicate threshold check — errors, no auto-fix.
    EDataValidationResult Result = EDataValidationResult::Valid;
    for (int32 i = 1; i < Entries.Num(); ++i)
    {
        if (FMath::IsNearlyEqual(
            Entries[i].RollThreshold, Entries[i - 1].RollThreshold, KINDA_SMALL_NUMBER))
        {
            Context.AddError(FText::Format(
                LOCTEXT("DuplicateThreshold",
                    "Entries {0} and {1} share RollThreshold {2}. "
                    "Duplicate thresholds cause non-deterministic reward selection."),
                FText::AsNumber(i - 1),
                FText::AsNumber(i),
                FText::AsNumber(Entries[i].RollThreshold)));
            Result = EDataValidationResult::Invalid;
        }
    }
    return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
