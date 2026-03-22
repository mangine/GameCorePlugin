// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DialogueNode.h"
#include "DialogueNode_Jump.generated.h"

// Unconditional jump to any node index.
// Handles loops and path convergence.
// Infinite loop guard: FDialogueSimulator::Advance aborts with AssetError
// after MaxAutoSteps (256) consecutive Continue actions.
UCLASS(meta = (DisplayName = "Jump"))
class GAMECORE_API UDialogueNode_Jump : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump")
    int32 TargetNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;

#if WITH_EDITOR
    virtual FString GetPreviewLabel() const override;
#endif
};
