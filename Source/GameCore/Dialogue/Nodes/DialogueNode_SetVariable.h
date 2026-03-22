// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DialogueNode.h"
#include "Dialogue/DialogueTypes.h"
#include "DialogueNode_SetVariable.generated.h"

// Writes a named value into FDialogueSession::Variables. No client involvement.
UCLASS(meta = (DisplayName = "Set Variable"))
class GAMECORE_API UDialogueNode_SetVariable : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variable")
    FName VariableName;

    // Value to write. FDialogueVariant holds bool, int32, or FGameplayTag.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variable")
    FDialogueVariant Value;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variable")
    int32 NextNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;

#if WITH_EDITOR
    virtual FString GetPreviewLabel() const override;
#endif
};
