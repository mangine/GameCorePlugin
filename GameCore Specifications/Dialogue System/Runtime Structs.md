# Runtime Structs

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)

---

## FDialogueVariant

A discriminated union holding one of: `bool`, `int32`, or `FGameplayTag`. Used in session variables and node configurations.

```cpp
// File: Dialogue/DialogueTypes.h

UENUM()
enum class EDialogueVariantType : uint8
{
    Bool,
    Int,
    Tag,
};

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

    bool   AsBool() const { return BoolValue; }
    int32  AsInt()  const { return IntValue;  }
    FGameplayTag AsTag() const { return TagValue; }
};
```

---

## FDialogueStackFrame

One entry in the session's asset/node stack. The stack enables `UDialogueNode_SubDialogue` tunneling.

```cpp
struct FDialogueStackFrame
{
    // The asset being executed at this stack level.
    TObjectPtr<UDialogueAsset> Asset;
    // The node to execute next within Asset.
    int32 CurrentNodeIndex;
    // The node to return to in the *parent* frame when this frame's asset ends normally.
    // INDEX_NONE on the root frame.
    int32 ReturnNodeIndex;
};
```

---

## FDialogueSession

All mutable server-side state for one active conversation. Owned by `UDialogueComponent`. One instance per active session.

```cpp
// File: Dialogue/DialogueTypes.h

struct FDialogueSession
{
    // Unique identifier for this session. Used in all RPCs and delegates.
    FGuid SessionID;

    // All participants. Participants[0] is the default Chooser unless ResolveChooser overrides.
    TArray<TWeakObjectPtr<AActor>> Participants;

    // The participant whose choice RPCs are accepted. Resolved once at session start.
    TWeakObjectPtr<AActor> Chooser;

    // Asset execution stack. Index 0 is the root asset.
    // Push on SubDialogue, pop when a sub-asset ends.
    TArray<FDialogueStackFrame> AssetStack;

    // Per-session named variables. Written by UDialogueNode_SetVariable,
    // readable by UDialogueNode_Condition via FRequirementContext injection.
    TMap<FName, FDialogueVariant> Variables;

    // Handle for the active choice timeout timer. Invalid when no timeout is running.
    FTimerHandle TimeoutHandle;

    // Whether the session is paused waiting for external input.
    bool bWaiting = false;

    // --- Accessors ---

    // Returns the current stack frame. Always valid while the session is active.
    FDialogueStackFrame& CurrentFrame() { return AssetStack.Last(); }
    const FDialogueStackFrame& CurrentFrame() const { return AssetStack.Last(); }

    // Convenience: returns the UWorld from the Chooser actor. Nullable — always check.
    UWorld* GetWorld() const;

    // Convenience: returns Chooser.Get(). May be null if Chooser disconnected.
    AActor* GetChooser() const { return Chooser.Get(); }

    bool IsValid() const { return SessionID.IsValid() && !AssetStack.IsEmpty(); }
};
```

> **Lifetime:** `FDialogueSession` is created by `UDialogueComponent::StartDialogue` and destroyed by `UDialogueComponent::EndSession`. It is never replicated — it is server-private.

---

## FDialogueClientChoice

One choice entry sent to the client. Contains only what the UI needs — no node indices.

```cpp
USTRUCT(BlueprintType)
struct FDialogueClientChoice
{
    GENERATED_BODY()

    // Index into the server's FDialogueChoiceConfig array. Sent back in the choice RPC.
    UPROPERTY()
    int32 ChoiceIndex = 0;

    // Localized label. Already resolved — client does not re-evaluate.
    UPROPERTY(BlueprintReadOnly)
    FText Label;

    // True if LockCondition failed. Client shows the choice greyed out.
    UPROPERTY(BlueprintReadOnly)
    bool bLocked = false;

    // Localized reason string, populated only when bLocked == true.
    UPROPERTY(BlueprintReadOnly)
    FText LockReasonText;
};
```

---

## FDialogueClientState

The full state snapshot replicated to participants when the session advances to a new displayable step. Sent via reliable ClientRPC on `UDialogueManagerComponent`.

```cpp
USTRUCT(BlueprintType)
struct FDialogueClientState
{
    GENERATED_BODY()

    // Session this state belongs to. Allows UDialogueManagerComponent to
    // route state to the correct UI when multiple sessions are active.
    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    // Speaker identity. Client UI resolves display name and portrait from its own registry.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag SpeakerTag;

    // Localized line text. Empty when this is a choice-only step with no preceding line.
    UPROPERTY(BlueprintReadOnly)
    FText LineText;

    // Soft ref to voice-over. Client loads and plays. Nullable.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<USoundBase> VoiceCue;

    // Non-empty when the session is waiting for a choice.
    UPROPERTY(BlueprintReadOnly)
    TArray<FDialogueClientChoice> Choices;

    // True when waiting for choice, false when waiting for line ACK.
    UPROPERTY(BlueprintReadOnly)
    bool bWaitingForChoice = false;

    // True when waiting for line ACK.
    UPROPERTY(BlueprintReadOnly)
    bool bWaitingForACK = false;

    // True if this participant is an Observer (not the Chooser). UI may suppress choice input.
    UPROPERTY(BlueprintReadOnly)
    bool bIsObserver = false;

    // Timeout in seconds. 0 = no timeout. Sent once; client counts down locally.
    UPROPERTY(BlueprintReadOnly)
    float TimeoutSeconds = 0.0f;

    // Remaining time when this state was pushed. Client adds local elapsed time.
    UPROPERTY(BlueprintReadOnly)
    float TimeoutRemainingSeconds = 0.0f;
};
```

> **Design note:** No node indices, no asset references, no server-side topology leaks to the client. `ChoiceIndex` in `FDialogueClientChoice` is an opaque cookie sent back verbatim in the RPC — the server maps it back to `FDialogueChoiceConfig` internally.

---

## FDialogueEventMessage

GMS message payload broadcast by `UDialogueNode_Event`.

```cpp
USTRUCT(BlueprintType)
struct FDialogueEventMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    // The Chooser (or sole instigator) at the time the event fired.
    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> Instigator;

    // Optional secondary tag payload. Meaning is event-specific.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag PayloadTag;
};
```
