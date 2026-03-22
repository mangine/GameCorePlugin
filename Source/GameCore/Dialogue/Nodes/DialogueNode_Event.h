// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DialogueNode.h"
#include "DialogueNode_Event.generated.h"

// Broadcasts a GMS message. Server advances immediately — no client involvement.
UCLASS(meta = (DisplayName = "Event"))
class GAMECORE_API UDialogueNode_Event : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Tag identifying the GMS event channel. Game code registers listeners on these tags.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event",
        meta = (Categories = "DialogueEvent"))
    FGameplayTag EventTag;

    // Optional tag payload passed in FDialogueEventMessage.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
    FGameplayTag PayloadTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
    int32 NextNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;

#if WITH_EDITOR
    virtual FString GetPreviewLabel() const override;
#endif
};
