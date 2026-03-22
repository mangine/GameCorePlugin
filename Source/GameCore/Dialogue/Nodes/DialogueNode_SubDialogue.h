// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DialogueNode.h"
#include "DialogueNode_SubDialogue.generated.h"

class UDialogueAsset;

// Pushes a nested UDialogueAsset onto the session stack.
// When the sub-asset ends normally, execution resumes at ReturnNodeIndex in the parent asset.
UCLASS(meta = (DisplayName = "Sub-Dialogue"))
class GAMECORE_API UDialogueNode_SubDialogue : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SubDialogue")
    TObjectPtr<UDialogueAsset> SubAsset;

    // Node in the *parent* asset to return to when the sub-dialogue ends normally.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SubDialogue")
    int32 ReturnNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;

#if WITH_EDITOR
    virtual FString GetPreviewLabel() const override;
#endif
};
