// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueNode_SubDialogue.h"
#include "Dialogue/Assets/DialogueAsset.h"
#include "Core/Backend/GameCoreBackend.h"

// Tag for logging — defined in DialogueSimulator.cpp
DECLARE_LOG_CATEGORY_EXTERN(LogDialogue, Log, All);

FDialogueStepResult UDialogueNode_SubDialogue::Execute(FDialogueSession& Session) const
{
    if (!SubAsset)
    {
        UE_LOG(LogDialogue, Error,
            TEXT("UDialogueNode_SubDialogue: SubAsset is null — skipping sub-dialogue."));
        // Graceful fallback: skip the sub-dialogue and return to ReturnNodeIndex.
        FDialogueStepResult Result;
        Result.Action   = EDialogueStepAction::Continue;
        Result.NextNode = ReturnNodeIndex;
        return Result;
    }

    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::SubDialoguePush;
    Result.SubAsset = SubAsset;
    // ReturnNodeIndex is stored as the return address on the current stack frame
    // before FDialogueSimulator pushes the new frame.
    Result.NextNode = ReturnNodeIndex;
    return Result;
}

#if WITH_EDITOR
FString UDialogueNode_SubDialogue::GetPreviewLabel() const
{
    return FString::Printf(TEXT("SubDialogue: %s"),
        SubAsset ? *SubAsset->GetName() : TEXT("<null>"));
}
#endif
