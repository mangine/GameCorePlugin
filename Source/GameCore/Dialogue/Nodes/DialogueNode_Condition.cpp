// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueNode_Condition.h"
#include "Dialogue/DialogueSimulator.h"
#include "Requirements/RequirementComposite.h"

DEFINE_LOG_CATEGORY_STATIC(LogDialogueCondition, Log, All);

FDialogueStepResult UDialogueNode_Condition::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    Result.Action = EDialogueStepAction::Continue;

    if (Condition)
    {
        // KI-5 fix: Use FDialogueSimulator::BuildContext — NOT a local BuildContextFromSession call.
        FRequirementContext Ctx = FDialogueSimulator::BuildContext(Session);
        const bool bPassed = Condition->Evaluate(Ctx).bPassed;
        Result.NextNode = bPassed ? TrueNodeIndex : FalseNodeIndex;
    }
    else
    {
        UE_LOG(LogDialogueCondition, Warning,
            TEXT("UDialogueNode_Condition has no Condition set — taking True branch."));
        Result.NextNode = TrueNodeIndex;
    }

    return Result;
}

#if WITH_EDITOR
FString UDialogueNode_Condition::GetPreviewLabel() const
{
    return FString::Printf(TEXT("Condition -> True:%d / False:%d"), TrueNodeIndex, FalseNodeIndex);
}
#endif
