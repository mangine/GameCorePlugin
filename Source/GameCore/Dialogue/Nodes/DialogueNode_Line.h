// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DialogueNode.h"
#include "DialogueNode_Line.generated.h"

class USoundBase;

// Displays a single line of dialogue from a speaker.
// Optionally waits for client ACK before advancing.
UCLASS(meta = (DisplayName = "Line"))
class GAMECORE_API UDialogueNode_Line : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Tag identifying the speaker. Client UI resolves display name and portrait
    // from its own speaker registry keyed by this tag.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line",
        meta = (Categories = "Dialogue.Speaker"))
    FGameplayTag SpeakerTag;

    // Localized line text. MUST be a StringTable reference — validated on asset save.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
    FText LineText;

    // Soft reference to a voice-over cue. Nullable. Client loads and plays locally.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
    TSoftObjectPtr<USoundBase> VoiceCue;

    // If true, interpreter waits for Server_ReceiveACK before advancing.
    // If false, server advances immediately after pushing state to clients (cutscene lines).
    // NOTE: When false, PushClientState must still be called before advancing so clients
    // can display the line before it disappears. (See KI-4 in Architecture.md.)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
    bool bRequiresAck = true;

    // Index of the next node. INDEX_NONE = end session after this line.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
    int32 NextNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;

#if WITH_EDITOR
    virtual FString GetPreviewLabel() const override;
#endif
};
