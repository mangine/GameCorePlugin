#include "Quest/Data/QuestStageDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UQuestStageDefinition::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    if (!StageTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("QuestStageDefinition", "NoStageTag",
            "StageTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    if (bIsCompletionState && bIsFailureState)
    {
        Context.AddError(NSLOCTEXT("QuestStageDefinition", "BothTerminal",
            "Stage cannot be both a completion state and a failure state."));
        Result = EDataValidationResult::Invalid;
    }

    for (const FQuestProgressTrackerDef& Tracker : Trackers)
    {
        if (!Tracker.TrackerKey.IsValid())
        {
            Context.AddError(NSLOCTEXT("QuestStageDefinition", "InvalidTrackerKey",
                "All tracker entries must have a valid TrackerKey."));
            Result = EDataValidationResult::Invalid;
        }
    }

    return Result;
}
#endif
