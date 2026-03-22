// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Dialogue/DialogueEnums.h"
#include "DialogueAsset.generated.h"

class UDialogueNode;

// The authored content unit for a dialogue.
// Contains a flat array of instanced UDialogueNode objects and a start index.
// Shared (read-only) across all sessions that reference it — never modified at runtime.
UCLASS()
class GAMECORE_API UDialogueAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // Session mode. Single wraps the instigator in a one-element array internally.
    // Set once per asset — changing mid-session is not supported.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    EDialogueSessionMode SessionMode = EDialogueSessionMode::Single;

    // Index of the first node executed when a session starts.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    int32 StartNodeIndex = 0;

    // All nodes in this dialogue. Instanced — each is a UDialogueNode subclass
    // configured in this asset. Indices are stable after cook.
    UPROPERTY(EditAnywhere, Instanced, Category = "Dialogue")
    TArray<TObjectPtr<UDialogueNode>> Nodes;

    // Returns the node at Index, or nullptr if out of bounds.
    // Always check the return value before use.
    const UDialogueNode* GetNode(int32 Index) const;

#if WITH_EDITOR
    // Validates that all UDialogueNode_Line instances use StringTable-sourced FText.
    // Logs a warning for each violation. Raw string literals in line nodes are a
    // localization bug caught at save time, not at runtime.
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
