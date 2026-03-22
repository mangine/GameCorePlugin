// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Dialogue/DialogueTypes.h"
#include "DialogueNode.generated.h"

// Abstract base class for all dialogue nodes.
//
// CRITICAL CONSTRAINT: Nodes own no per-instance mutable state.
// They are shared read-only objects inside UDialogueAsset.
// Multiple simultaneous sessions may execute the same node object.
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UDialogueNode : public UObject
{
    GENERATED_BODY()

public:
    // Execute this node against the current session.
    // MUST be a pure function with respect to the node's own fields.
    // All mutable state lives in Session, never on the node itself.
    virtual FDialogueStepResult Execute(FDialogueSession& Session) const
        PURE_VIRTUAL(UDialogueNode::Execute, return {};);

#if WITH_EDITOR
    // Optional: editor display name shown in the preview tool log.
    virtual FString GetPreviewLabel() const { return GetClass()->GetName(); }
#endif
};
