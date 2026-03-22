// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueNode_SetVariable.h"

FDialogueStepResult UDialogueNode_SetVariable::Execute(FDialogueSession& Session) const
{
    if (!VariableName.IsNone())
        Session.Variables.Add(VariableName, Value);

    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::Continue;
    Result.NextNode = NextNodeIndex;
    return Result;
}

#if WITH_EDITOR
FString UDialogueNode_SetVariable::GetPreviewLabel() const
{
    return FString::Printf(TEXT("SetVariable: %s = %s"),
        *VariableName.ToString(),
        *Value.ToLogString());
}
#endif
