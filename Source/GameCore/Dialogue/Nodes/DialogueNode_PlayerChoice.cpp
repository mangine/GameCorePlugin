// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueNode_PlayerChoice.h"

FDialogueStepResult UDialogueNode_PlayerChoice::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::WaitForChoice;
    Result.NextNode = INDEX_NONE; // Resolved by FDialogueSimulator::ResolveChoice
    return Result;
}

#if WITH_EDITOR
FString UDialogueNode_PlayerChoice::GetPreviewLabel() const
{
    return FString::Printf(TEXT("PlayerChoice (%d choices)"), Choices.Num());
}
#endif
