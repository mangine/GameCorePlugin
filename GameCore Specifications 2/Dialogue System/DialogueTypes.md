# Dialogue System — Types

**File:** `Dialogue/DialogueTypes.h`

All runtime structs and GMS payload types for the Dialogue System, plus the `LogDialogue` category declaration.

---

## Log Category

```cpp
// Declared in DialogueTypes.h, defined in DialogueSimulator.cpp
GAMECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDialogue, Log, All);
```

All server-side errors and warnings use `FGameCoreBackend::GetLogging(TAG_Log_Dialogue)`.  
`UE_LOG(LogDialogue, ...)` is reserved for editor-only paths where the backend is unavailable (e.g. `FDialoguePreviewContext`).

---

## FDialogueVariant

A discriminated union holding one of: `bool`, `int32`, or `FGameplayTag`. Used in session variables (`FDialogueSession::Variables`) and `UDialogueNode_SetVariable`.

```cpp
USTRUCT(BlueprintType)
struct FDialogueVariant
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
```

---

## FDialogueStackFrame

One entry in `FDialogueSession::AssetStack`. Enables `UDialogueNode_SubDialogue` tunneling.

```cpp
struct FDialogueStackFrame
{
    // The asset being executed at this stack level.
    TObjectPtr<UDialogueAsset> Asset = nullptr;

    // Index of the node currently executing or waiting for input.
    // INVARIANT: Never advanced past the waiting node on WaitForACK/WaitForChoice.
    // The simulator advances it to Result.NextNode only on Continue.
    // This means Server_ReceiveACK and ResolveChoice can always read the waiting node
    // directly at CurrentNodeIndex without any secondary index storage.
    int32 CurrentNodeIndex = INDEX_NONE;

    // The node index in the parent frame to resume at when this frame's asset
    // ends normally. INDEX_NONE on the root frame — ending the root ends the session.
    int32 ReturnNodeIndex = INDEX_NONE;
};
```

---

## FDialogueSession

All mutable server-side state for one active conversation. Owned by `UDialogueComponent` in its `ActiveSessions` map. Never replicated.

```cpp
struct FDialogueSession
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
```

> **Lifetime:** Created by `UDialogueComponent::StartGroupDialogue`, destroyed by `UDialogueComponent::EndSession`. Server-private — never sent to clients.

---

## FDialogueStepResult

Return type of `UDialogueNode::Execute`. Consumed by `FDialogueSimulator::Advance`.

```cpp
struct FDialogueStepResult
{
    EDialogueStepAction Action    = EDialogueStepAction::Continue;
    int32               NextNode  = INDEX_NONE;  // For Continue: next node to advance to.
                                                  // For SubDialoguePush: return address on current frame.
    EDialogueEndReason  EndReason = EDialogueEndReason::Completed;  // Only meaningful on EndSession.

    // Populated only when Action == SubDialoguePush.
    TObjectPtr<UDialogueAsset> SubAsset = nullptr;
};
```

---

## FDialogueClientChoice

One choice entry sent to the client inside `FDialogueClientState`. Contains only what the UI needs — no server-internal indices are exposed.

```cpp
USTRUCT(BlueprintType)
struct FDialogueClientChoice
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
```

---

## FDialogueClientState

Full state snapshot sent to all participants when the session reaches a displayable step. Delivered via reliable `ClientRPC_ReceiveDialogueState` on `UDialogueManagerComponent`.

```cpp
USTRUCT(BlueprintType)
struct FDialogueClientState
{
    GENERATED_BODY()

    // Routes this state to the correct in-progress session on the client.
    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    // Speaker identity tag. Client UI resolves display name and portrait from its own registry.
    // The dialogue system never carries actor references for speaker identity.
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
```

---

## FDialogueEventMessage

GMS payload broadcast by `UDialogueNode_Event`.

```cpp
USTRUCT(BlueprintType)
struct FDialogueEventMessage
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
```

---

## GMS Session Lifecycle Payloads

```cpp
// Broadcast on TAG_DialogueEvent_Session_Started
USTRUCT(BlueprintType)
struct FDialogueSessionEventMessage
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
struct FDialogueSessionEndedMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> Instigator;

    UPROPERTY(BlueprintReadOnly)
    EDialogueEndReason Reason;

    // Optional reason tag from UDialogueNode_End::EndReasonTag.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag EndReasonTag;
};
```
