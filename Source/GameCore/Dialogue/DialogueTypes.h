// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DialogueEnums.h"
#include "DialogueTypes.generated.h"

// Forward declarations
class UDialogueAsset;
class UDialogueComponent;
class USoundBase;

// Log category — declared here, defined in DialogueSimulator.cpp.
// All server-side errors/warnings use FGameCoreBackend::GetLogging(TAG_Log_Dialogue).
// UE_LOG(LogDialogue, ...) is reserved for editor-only paths.
GAMECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDialogue, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
// FDialogueVariant
// ─────────────────────────────────────────────────────────────────────────────

// A discriminated union holding one of: bool, int32, or FGameplayTag.
// Used in session variables (FDialogueSession::Variables) and UDialogueNode_SetVariable.
USTRUCT(BlueprintType)
struct GAMECORE_API FDialogueVariant
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    EDialogueVariantType Type = EDialogueVariantType::Bool;

    UPROPERTY(EditAnywhere)
    bool BoolValue = false;

    UPROPERTY(EditAnywhere)
    int32 IntValue = 0;

    UPROPERTY(EditAnywhere)
    FGameplayTag TagValue;

    bool         AsBool() const { return BoolValue; }
    int32        AsInt()  const { return IntValue; }
    FGameplayTag AsTag()  const { return TagValue; }

    FString ToLogString() const
    {
        switch (Type)
        {
        case EDialogueVariantType::Bool: return BoolValue ? TEXT("true") : TEXT("false");
        case EDialogueVariantType::Int:  return FString::FromInt(IntValue);
        case EDialogueVariantType::Tag:  return TagValue.ToString();
        default:                         return TEXT("<unknown>");
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// FDialogueStackFrame
// ─────────────────────────────────────────────────────────────────────────────

// One entry in FDialogueSession::AssetStack. Enables UDialogueNode_SubDialogue tunneling.
struct GAMECORE_API FDialogueStackFrame
{
    // The asset being executed at this stack level.
    TObjectPtr<UDialogueAsset> Asset = nullptr;

    // Index of the node currently executing or waiting for input.
    // INVARIANT: Never advanced past the waiting node on WaitForACK/WaitForChoice.
    // The simulator advances it to Result.NextNode only on Continue.
    int32 CurrentNodeIndex = INDEX_NONE;

    // The node index in the parent frame to resume at when this frame's asset
    // ends normally. INDEX_NONE on the root frame.
    int32 ReturnNodeIndex = INDEX_NONE;
};

// ─────────────────────────────────────────────────────────────────────────────
// FDialogueSession
// ─────────────────────────────────────────────────────────────────────────────

// All mutable server-side state for one active conversation.
// Owned by UDialogueComponent in its ActiveSessions map. Never replicated.
struct GAMECORE_API FDialogueSession
{
    // Unique identifier. Used in all RPCs, delegates, and GMS events.
    FGuid SessionID;

    // All participants. Participants[0] is the default Chooser unless ResolveChooser overrides.
    TArray<TWeakObjectPtr<AActor>> Participants;

    // The participant whose choice RPCs are accepted. Resolved once at session start.
    TWeakObjectPtr<AActor> Chooser;

    // Asset execution stack. Index 0 is the root asset.
    // Pushed on SubDialogue entry, popped when a sub-asset ends normally.
    TArray<FDialogueStackFrame> AssetStack;

    // Per-session named variables. Written by UDialogueNode_SetVariable,
    // injected into FRequirementContext by FDialogueSimulator::BuildContext.
    TMap<FName, FDialogueVariant> Variables;

    // Handle for the active choice timeout timer. Invalid when no timeout is running.
    FTimerHandle TimeoutHandle;

    // True when the session is paused waiting for client input (choice or ACK).
    bool bWaiting = false;

    // --- Accessors ---

    FDialogueStackFrame&       CurrentFrame()       { return AssetStack.Last(); }
    const FDialogueStackFrame& CurrentFrame() const { return AssetStack.Last(); }

    UWorld* GetWorld() const
    {
        const AActor* C = Chooser.Get();
        return C ? C->GetWorld() : nullptr;
    }

    AActor* GetChooser() const { return Chooser.Get(); }

    bool IsValid() const { return SessionID.IsValid() && !AssetStack.IsEmpty(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// FDialogueStepResult
// ─────────────────────────────────────────────────────────────────────────────

// Return type of UDialogueNode::Execute. Consumed by FDialogueSimulator::Advance.
struct GAMECORE_API FDialogueStepResult
{
    EDialogueStepAction Action    = EDialogueStepAction::Continue;
    int32               NextNode  = INDEX_NONE;  // For Continue: next node to advance to.
                                                  // For SubDialoguePush: return address on current frame.
    EDialogueEndReason  EndReason = EDialogueEndReason::Completed;  // Only meaningful on EndSession.

    // Populated only when Action == SubDialoguePush.
    TObjectPtr<UDialogueAsset> SubAsset = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// FDialogueClientChoice
// ─────────────────────────────────────────────────────────────────────────────

// One choice entry sent to the client inside FDialogueClientState.
USTRUCT(BlueprintType)
struct GAMECORE_API FDialogueClientChoice
{
    GENERATED_BODY()

    // Opaque cookie echoed back verbatim in ServerRPC_SubmitChoice.
    // Server maps this to FDialogueChoiceConfig::TargetNodeIndex on receipt.
    UPROPERTY(BlueprintReadOnly)
    int32 ChoiceIndex = 0;

    // Localized label. Resolved server-side — client never re-evaluates.
    UPROPERTY(BlueprintReadOnly)
    FText Label;

    // True if LockCondition failed on the server. UI shows this choice greyed out.
    UPROPERTY(BlueprintReadOnly)
    bool bLocked = false;

    // Populated only when bLocked == true. Localized failure reason for tooltip.
    UPROPERTY(BlueprintReadOnly)
    FText LockReasonText;
};

// ─────────────────────────────────────────────────────────────────────────────
// FDialogueClientState
// ─────────────────────────────────────────────────────────────────────────────

// Full state snapshot sent to all participants when the session reaches a displayable step.
// Delivered via reliable ClientRPC_ReceiveDialogueState on UDialogueManagerComponent.
USTRUCT(BlueprintType)
struct GAMECORE_API FDialogueClientState
{
    GENERATED_BODY()

    // Routes this state to the correct in-progress session on the client.
    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    // Speaker identity tag. Client UI resolves display name and portrait from its own registry.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag SpeakerTag;

    // Localized line text.
    UPROPERTY(BlueprintReadOnly)
    FText LineText;

    // Soft ref to a voice-over cue. Client loads and plays locally. Nullable.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<USoundBase> VoiceCue;

    // Non-empty when the session is waiting for a player choice.
    UPROPERTY(BlueprintReadOnly)
    TArray<FDialogueClientChoice> Choices;

    // Exactly one of these is true when the session is waiting for input.
    UPROPERTY(BlueprintReadOnly)
    bool bWaitingForChoice = false;

    UPROPERTY(BlueprintReadOnly)
    bool bWaitingForACK = false;

    // True for Observers in a Group session — UI should suppress choice input.
    UPROPERTY(BlueprintReadOnly)
    bool bIsObserver = false;

    // 0 = no timeout. Sent once on state push; client counts down locally.
    UPROPERTY(BlueprintReadOnly)
    float TimeoutSeconds = 0.0f;

    // Server-side remaining time at the moment this state was pushed.
    // Client adds local elapsed time to drive the countdown UI.
    UPROPERTY(BlueprintReadOnly)
    float TimeoutRemainingSeconds = 0.0f;

    // The UDialogueComponent that owns this session.
    // UDialogueManagerComponent populates SessionOwners from this on ClientRPC receipt.
    // Requires UDialogueComponent to have SetIsReplicated(true) so a NetGUID exists.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UDialogueComponent> OwnerComponent;
};

// ─────────────────────────────────────────────────────────────────────────────
// FDialogueEventMessage
// ─────────────────────────────────────────────────────────────────────────────

// GMS payload broadcast by UDialogueNode_Event.
USTRUCT(BlueprintType)
struct GAMECORE_API FDialogueEventMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    // Chooser or sole instigator at the time the event node executed.
    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> Instigator;

    // Game-specific payload. Meaning is event-type-specific.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag PayloadTag;
};

// ─────────────────────────────────────────────────────────────────────────────
// GMS Session Lifecycle Payloads
// ─────────────────────────────────────────────────────────────────────────────

// Broadcast on TAG_DialogueEvent_Session_Started
USTRUCT(BlueprintType)
struct GAMECORE_API FDialogueSessionEventMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    // The Chooser or sole instigator.
    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> Instigator;

    // The asset that started. TSoftObjectPtr to avoid outliving the asset after session end.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UDialogueAsset> Asset;
};

// Broadcast on TAG_DialogueEvent_Session_Ended
USTRUCT(BlueprintType)
struct GAMECORE_API FDialogueSessionEndedMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> Instigator;

    UPROPERTY(BlueprintReadOnly)
    EDialogueEndReason Reason = EDialogueEndReason::Completed;

    // Optional reason tag from UDialogueNode_End::EndReasonTag.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag EndReasonTag;
};

// ─────────────────────────────────────────────────────────────────────────────
// FDialogueRequirementContext
// ─────────────────────────────────────────────────────────────────────────────

// Dialogue-specific payload passed via FRequirementContext::Data to requirement evaluation.
// Populated by FDialogueSimulator::BuildContext from session state.
USTRUCT(BlueprintType)
struct GAMECORE_API FDialogueRequirementContext
{
    GENERATED_BODY()

    // The Chooser actor (or nullptr in editor preview).
    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> ChooserActor;

    // Tags from the Chooser (via ITaggedInterface) plus Tag-type session variables.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTagContainer SourceTags;

    // Named bool/int values from session variables — keyed by variable name.
    // Bool values are stored as 0/1. Int values stored as-is.
    UPROPERTY(BlueprintReadOnly)
    TMap<FName, int32> NamedIntegers;
};
