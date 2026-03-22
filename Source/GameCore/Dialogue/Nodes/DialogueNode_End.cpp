// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueNode_End.h"

FDialogueStepResult UDialogueNode_End::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    Result.Action    = EDialogueStepAction::EndSession;
    Result.EndReason = EDialogueEndReason::Completed;
    return Result;
}

#if WITH_EDITOR
FString UDialogueNode_End::GetPreviewLabel() const
{
    if (EndReasonTag.IsValid())
        return FString::Printf(TEXT("End [%s]"), *EndReasonTag.ToString());
    return TEXT("End");
}
#endif
