// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueNode_Jump.h"

FDialogueStepResult UDialogueNode_Jump::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::Continue;
    Result.NextNode = TargetNodeIndex;
    return Result;
}

#if WITH_EDITOR
FString UDialogueNode_Jump::GetPreviewLabel() const
{
    return FString::Printf(TEXT("Jump -> Node %d"), TargetNodeIndex);
}
#endif
