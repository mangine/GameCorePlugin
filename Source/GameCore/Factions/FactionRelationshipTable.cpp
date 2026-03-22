// Copyright GameCore Plugin. All Rights Reserved.
#include "FactionRelationshipTable.h"

#define LOCTEXT_NAMESPACE "FactionSystem"

#if WITH_EDITOR

EDataValidationResult UFactionRelationshipTable::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    // Duplicate faction entries.
    TSet<FSoftObjectPath> SeenFactions;
    for (const TSoftObjectPtr<UFactionDefinition>& Def : Factions)
    {
        if (!SeenFactions.Add(Def.ToSoftObjectPath()).IsAlreadyInSet()) continue;
        Context.AddError(FText::Format(
            LOCTEXT("DuplicateFaction", "UFactionRelationshipTable: duplicate faction entry {0}."),
            FText::FromString(Def.GetAssetName())));
        Result = EDataValidationResult::Invalid;
    }

    // Duplicate explicit pairs.
    TSet<uint32> SeenPairs;
    for (const FFactionRelationshipOverride& Override : ExplicitRelationships)
    {
        const uint32 Hash = GetTypeHash(FFactionSortedPair(Override.FactionA, Override.FactionB));
        if (!SeenPairs.Add(Hash).IsAlreadyInSet()) continue;
        Context.AddError(FText::Format(
            LOCTEXT("DuplicatePair",
                "UFactionRelationshipTable: duplicate explicit pair {0} / {1}."),
            FText::FromName(Override.FactionA.GetTagName()),
            FText::FromName(Override.FactionB.GetTagName())));
        Result = EDataValidationResult::Invalid;
    }

    return Result;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
