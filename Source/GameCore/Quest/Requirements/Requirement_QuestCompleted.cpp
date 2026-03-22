#include "Quest/Requirements/Requirement_QuestCompleted.h"
#include "Quest/Components/QuestComponent.h"
#include "GameFramework/PlayerState.h"

#define LOCTEXT_NAMESPACE "QuestRequirements"

FRequirementResult URequirement_QuestCompleted::Evaluate(
    const FRequirementContext& Context) const
{
    const FQuestEvaluationContext* CtxData =
        Context.Data.GetPtr<FQuestEvaluationContext>();
    if (!CtxData || !CtxData->PlayerState)
        return FRequirementResult::Fail(LOCTEXT("NoCtx", "No player context."));

    const UQuestComponent* QC =
        CtxData->PlayerState->FindComponentByClass<UQuestComponent>();
    if (!QC)
        return FRequirementResult::Fail(LOCTEXT("NoQC", "No quest component."));

    return QC->CompletedQuestTags.HasTag(RequiredQuestCompletedTag)
        ? FRequirementResult::Pass()
        : FRequirementResult::Fail(LOCTEXT("NotDone", "Required quest not yet completed."));
}

#undef LOCTEXT_NAMESPACE
