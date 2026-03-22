// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DialogueNode.h"
#include "DialogueNode_PlayerChoice.generated.h"

class URequirement_Composite;

// Per-choice configuration. Embedded in the node asset.
USTRUCT(BlueprintType)
struct GAMECORE_API FDialogueChoiceConfig
{
    GENERATED_BODY()

    // Label shown to the player. MUST be a StringTable reference.
    UPROPERTY(EditAnywhere)
    FText Label;

    // Optional lock condition. If set and fails, the choice is shown locked.
    // Evaluated server-side at Execute time and again in ResolveChoice (anti-cheat).
    UPROPERTY(EditAnywhere, Instanced)
    TObjectPtr<URequirement_Composite> LockCondition;

    // Failure reason shown when locked. MUST be a StringTable reference if LockCondition is set.
    UPROPERTY(EditAnywhere)
    FText LockReasonText;

    // Node to advance to when this choice is selected.
    UPROPERTY(EditAnywhere)
    int32 TargetNodeIndex = INDEX_NONE;
};

// Presents a list of choices to the Chooser.
// The session waits until Server_ReceiveChoice arrives or timeout expires.
UCLASS(meta = (DisplayName = "Player Choice"))
class GAMECORE_API UDialogueNode_PlayerChoice : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice")
    TArray<FDialogueChoiceConfig> Choices;

    // 0 = no timeout. When > 0, server auto-submits DefaultChoiceIndex on expiry.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice",
        meta = (ClampMin = "0.0"))
    float TimeoutSeconds = 0.0f;

    // Index into Choices selected automatically on timeout.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice")
    int32 DefaultChoiceIndex = 0;

    // In Group sessions: whether Observers see choices as read-only.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice")
    bool bShowChoicesToObservers = false;

    // Called by FDialogueSimulator::Advance. Returns WaitForChoice.
    // Display data (choices, locks) is read by BuildClientState from this node.
    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;

#if WITH_EDITOR
    virtual FString GetPreviewLabel() const override;
#endif
};
