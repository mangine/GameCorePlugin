// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DialogueNode.h"
#include "DialogueNode_End.generated.h"

// Terminates the session.
// EndReasonTag is read by UDialogueComponent::EndSession when the session completed naturally.
UCLASS(meta = (DisplayName = "End"))
class GAMECORE_API UDialogueNode_End : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Optional tag sent in OnDialogueEnded broadcast and the GMS Session.Ended event.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "End",
        meta = (Categories = "DialogueEvent.End"))
    FGameplayTag EndReasonTag;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;

#if WITH_EDITOR
    virtual FString GetPreviewLabel() const override;
#endif
};
