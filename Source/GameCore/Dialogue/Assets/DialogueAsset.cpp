// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueAsset.h"
#include "Dialogue/Nodes/DialogueNode.h"
#include "Dialogue/Nodes/DialogueNode_Line.h"
#include "Dialogue/Nodes/DialogueNode_PlayerChoice.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const UDialogueNode* UDialogueAsset::GetNode(int32 Index) const
{
    if (!Nodes.IsValidIndex(Index))
        return nullptr;
    return Nodes[Index].Get();
}

#if WITH_EDITOR
EDataValidationResult UDialogueAsset::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    // Validate StartNodeIndex
    if (!Nodes.IsValidIndex(StartNodeIndex))
    {
        Context.AddError(FText::FromString(FString::Printf(
            TEXT("StartNodeIndex %d is out of range (Nodes has %d entries)."),
            StartNodeIndex, Nodes.Num())));
        Result = EDataValidationResult::Invalid;
    }

    for (int32 i = 0; i < Nodes.Num(); ++i)
    {
        const UDialogueNode* Node = Nodes[i].Get();
        if (!Node)
        {
            Context.AddError(FText::FromString(FString::Printf(
                TEXT("Node at index %d is null."), i)));
            Result = EDataValidationResult::Invalid;
            continue;
        }

        // Validate line text uses StringTable references
        if (const UDialogueNode_Line* LineNode = Cast<UDialogueNode_Line>(Node))
        {
            if (!LineNode->LineText.IsEmpty() && !LineNode->LineText.IsFromStringTable())
            {
                Context.AddWarning(FText::FromString(FString::Printf(
                    TEXT("Node[%d] (Line): LineText is not from a StringTable. "
                         "All dialogue text must be StringTable-sourced for localization."), i)));
                Result = EDataValidationResult::Invalid;
            }
        }

        // Validate choice labels in PlayerChoice nodes
        if (const UDialogueNode_PlayerChoice* ChoiceNode = Cast<UDialogueNode_PlayerChoice>(Node))
        {
            for (int32 j = 0; j < ChoiceNode->Choices.Num(); ++j)
            {
                const FDialogueChoiceConfig& Choice = ChoiceNode->Choices[j];
                if (!Choice.Label.IsEmpty() && !Choice.Label.IsFromStringTable())
                {
                    Context.AddWarning(FText::FromString(FString::Printf(
                        TEXT("Node[%d] (PlayerChoice), Choice[%d]: Label is not from a StringTable."), i, j)));
                    Result = EDataValidationResult::Invalid;
                }
                if (Choice.LockCondition && !Choice.LockReasonText.IsEmpty()
                    && !Choice.LockReasonText.IsFromStringTable())
                {
                    Context.AddWarning(FText::FromString(FString::Printf(
                        TEXT("Node[%d] (PlayerChoice), Choice[%d]: LockReasonText is not from a StringTable."), i, j)));
                    Result = EDataValidationResult::Invalid;
                }
            }
        }
    }

    return Result;
}
#endif
