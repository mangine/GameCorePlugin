// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueNode_Line.h"

FDialogueStepResult UDialogueNode_Line::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    // Display data (SpeakerTag, LineText, VoiceCue) is read by UDialogueComponent::BuildClientState
    // from the node at CurrentNodeIndex. It is NOT carried in FDialogueStepResult.
    //
    // KI-4 fix: for bRequiresAck=false lines, UDialogueComponent::RunSession must call
    // PushClientState BEFORE advancing. This node returns Continue but the component
    // handles the push. See UDialogueComponent::RunSession for the KI-4 handling.
    Result.Action   = bRequiresAck ? EDialogueStepAction::WaitForACK : EDialogueStepAction::Continue;
    Result.NextNode = NextNodeIndex;
    return Result;
}

#if WITH_EDITOR
FString UDialogueNode_Line::GetPreviewLabel() const
{
    return FString::Printf(TEXT("Line [%s]: %s"),
        *SpeakerTag.ToString(),
        *LineText.ToString());
}
#endif
