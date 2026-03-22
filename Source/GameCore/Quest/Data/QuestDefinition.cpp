#include "Quest/Data/QuestDefinition.h"
#include "Quest/StateMachine/QuestStateNode.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UQuestDefinition::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    if (!QuestId.IsValid())
    {
        Context.AddError(NSLOCTEXT("QuestDefinition", "NoQuestId", "QuestId must be set."));
        Result = EDataValidationResult::Invalid;
    }

    if (!QuestCompletedTag.IsValid() &&
        (Lifecycle == EQuestLifecycle::SingleAttempt ||
         Lifecycle == EQuestLifecycle::RetryUntilComplete))
    {
        Context.AddError(NSLOCTEXT("QuestDefinition", "NoCompletedTag",
            "QuestCompletedTag must be set for SingleAttempt and RetryUntilComplete quests."));
        Result = EDataValidationResult::Invalid;
    }

    // Validate stage flag consistency between UQuestStageDefinition and UQuestStateNode.
    if (StageGraph)
    {
        for (const TObjectPtr<UQuestStageDefinition>& Stage : Stages)
        {
            if (!Stage) continue;

            const UQuestStateNode* Node = Cast<UQuestStateNode>(
                StageGraph->FindNode(Stage->StageTag));

            if (!Node)
            {
                Context.AddError(FText::Format(
                    NSLOCTEXT("QuestDefinition", "MissingNode",
                        "Stage '{0}' has no matching UQuestStateNode in StageGraph."),
                    FText::FromString(Stage->StageTag.ToString())));
                Result = EDataValidationResult::Invalid;
                continue;
            }

            if (Node->bIsCompletionState != Stage->bIsCompletionState ||
                Node->bIsFailureState    != Stage->bIsFailureState)
            {
                Context.AddError(FText::Format(
                    NSLOCTEXT("QuestDefinition", "FlagMismatch",
                        "Stage '{0}': bIsCompletionState / bIsFailureState mismatch "
                        "between UQuestStageDefinition and UQuestStateNode."),
                    FText::FromString(Stage->StageTag.ToString())));
                Result = EDataValidationResult::Invalid;
            }
        }
    }
    else if (!Stages.IsEmpty())
    {
        Context.AddError(NSLOCTEXT("QuestDefinition", "NoStageGraph",
            "StageGraph must be set when Stages are defined."));
        Result = EDataValidationResult::Invalid;
    }

    // Convention: leaf tag name must match the asset file name.
    const FString TagStr  = QuestId.GetTagName().ToString();
    const int32   LastDot = TagStr.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
    const FName   LeafName(*TagStr.RightChop(LastDot + 1));
    if (LeafName != GetFName())
    {
        Context.AddWarning(FText::Format(
            NSLOCTEXT("QuestDefinition", "NameMismatch",
                "QuestId leaf tag '{0}' does not match asset name '{1}'. "
                "UQuestRegistrySubsystem path resolution will fail at runtime."),
            FText::FromName(LeafName),
            FText::FromName(GetFName())));
    }

    return Result;
}
#endif
