// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DialogueNode.h"
#include "DialogueNode_Condition.generated.h"

class URequirement_Composite;

// Evaluates a requirement server-side and branches.
// No client involvement, no client state push.
// KI-5 fix: uses FDialogueSimulator::BuildContext — NOT any local BuildContextFromSession.
UCLASS(meta = (DisplayName = "Condition Branch"))
class GAMECORE_API UDialogueNode_Condition : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Evaluated against the session Chooser via FDialogueSimulator::BuildContext.
    UPROPERTY(EditAnywhere, Instanced, Category = "Condition")
    TObjectPtr<URequirement_Composite> Condition;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
    int32 TrueNodeIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
    int32 FalseNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;

#if WITH_EDITOR
    virtual FString GetPreviewLabel() const override;
#endif
};
