// Copyright GameCore Plugin. All Rights Reserved.
#include "Journal/JournalCollectionDefinition.h"
#include "Journal/JournalEntryDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UJournalCollectionDefinition::IsDataValid(
    FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    if (!CollectionTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Journal", "NoCollTag",
            "JournalCollectionDefinition: CollectionTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    if (!TrackTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Journal", "NoTrackTagColl",
            "JournalCollectionDefinition: TrackTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    // Circular reference detection via DFS with visited set.
    // Uses backtracking (remove on exit) to correctly detect cycles rather than
    // DAG branches that share a node — two independent paths to the same node
    // are valid; a path that loops back to an ancestor is not.
    TSet<FGameplayTag> Visited;
    Visited.Add(CollectionTag);

    TFunction<bool(const UJournalCollectionDefinition*)> CheckCycle =
        [&](const UJournalCollectionDefinition* Node) -> bool
    {
        for (const TSoftObjectPtr<UJournalCollectionDefinition>& Sub : Node->SubCollections)
        {
            const UJournalCollectionDefinition* SubDef = Sub.Get();
            if (!SubDef) continue;
            if (Visited.Contains(SubDef->CollectionTag))
            {
                Context.AddError(FText::Format(
                    NSLOCTEXT("Journal", "CircularSub",
                        "JournalCollectionDefinition: Circular sub-collection reference detected involving '{0}'."),
                    FText::FromString(SubDef->CollectionTag.ToString())));
                return true; // cycle found
            }
            Visited.Add(SubDef->CollectionTag);
            if (CheckCycle(SubDef)) return true;
            Visited.Remove(SubDef->CollectionTag);
        }
        return false;
    };

    if (CheckCycle(this))
        Result = EDataValidationResult::Invalid;

    return Result;
}
#endif
