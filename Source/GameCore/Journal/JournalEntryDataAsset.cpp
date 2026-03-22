// Copyright GameCore Plugin. All Rights Reserved.
#include "Journal/JournalEntryDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UJournalEntryDataAsset::IsDataValid(
    FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    if (!EntryTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Journal", "NoEntryTag",
            "JournalEntryDataAsset: EntryTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    if (!TrackTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Journal", "NoTrackTag",
            "JournalEntryDataAsset: TrackTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    return Result;
}
#endif
