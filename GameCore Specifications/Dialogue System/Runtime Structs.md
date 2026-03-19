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

    bool         AsBool() const { return BoolValue; }
    int32        AsInt()  const { return IntValue;  }
    FGameplayTag AsTag()  const { return TagValue;  }

    // Converts this variant to a string suitable for log output.
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

One entry in the session's asset/node stack. The stack enables `UDialogueNode_SubDialogue` tunneling.

```cpp
// File: Dialogue/DialogueTypes.h

struct FDialogueStackFrame
{
    // The asset being executed at this stack level.
    TObjectPtr<UDialogueAsset> Asset = nullptr;

    // Index of the node currently executing (or waiting for input).
    // The simulator always executes the node at this index, then updates it
    // to the NEXT node index only on Continue. On Wait, this index stays pointing
    // at the waiting node so that ResolveChoice / ResolveACK can retrieve it directly.
    int32 CurrentNodeIndex = INDEX_NONE;

    // The node index in the *parent* frame to resume at when this frame's asset
    // ends normally. INDEX_NONE on the root frame — ending the root ends the session.
    int32 ReturnNodeIndex = INDEX_NONE;
};
```

> **Critical invariant:** `CurrentNodeIndex` always points at the **currently executing or currently waiting** node — never the "next" node. The simulator advances `CurrentNodeIndex` to `Result.NextNode` only after a successful `Continue`. On `WaitForACK` or `WaitForChoice`, `CurrentNodeIndex` is left unchanged so that `ResolveChoice` and `Server_ReceiveACK` can retrieve the waiting node index without any arithmetic or secondary storage.

---

## FDialogueSession

All mutable server-side state for one active conversation. Owned by `UDialogueComponent`. One instance per active session.

```cpp
// File: Dialogue/DialogueTypes.h

struct FDialogueSession
{
    // Unique identifier. Used in all RPCs, delegates, and GMS events.
    FGuid SessionID;

    // All participants. Participants[0] is the default Chooser unless ResolveChooser overrides.
    TArray<TWeakObjectPtr<AActor>> Participants;

    // The participant whose choice RPCs are accepted. Resolved once at session start.
    TWeakObjectPtr<AActor> Chooser;

    // Asset execution stack. Index 0 is the root asset.
    // Pushed on SubDialogue entry, popped when a sub-asset ends.
    TArray<FDialogueStackFrame> AssetStack;

    // Per-session named variables. Written by UDialogueNode_SetVariable,
    // readable by UDialogueNode_Condition via FRequirementContext injection.
    TMap<FName, FDialogueVariant> Variables;

    // Handle for the active choice timeout timer. Invalid when no timeout is running.
    FTimerHandle TimeoutHandle;

    // True when the session is paused waiting for client input (choice or ACK).
    bool bWaiting = false;

    // --- Accessors ---

    FDialogueStackFrame& CurrentFrame()             { return AssetStack.Last(); }
    const FDialogueStackFrame& CurrentFrame() const { return AssetStack.Last(); }

    // Returns the UWorld via the Chooser. May be null — always check.
    UWorld* GetWorld() const
    {
        const AActor* C = Chooser.Get();
        return C ? C->GetWorld() : nullptr;
    }

    AActor* GetChooser() const { return Chooser.Get(); }

    bool IsValid() const { return SessionID.IsValid() && !AssetStack.IsEmpty(); }
};
```

> **Lifetime:** Created by `UDialogueComponent::StartGroupDialogue`, destroyed by `UDialogueComponent::EndSession`. Never replicated — server-private.

---

## FDialogueClientChoice

One choice entry sent to the client. Contains only what the UI needs.

```cpp
// File: Dialogue/DialogueTypes.h

USTRUCT(BlueprintType)
struct FDialogueClientChoice
{
    GENERATED_BODY()

    // Opaque cookie echoed back verbatim in ServerRPC_SubmitChoice.
    // Server maps this back to FDialogueChoiceConfig::TargetNodeIndex.
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
// File: Dialogue/DialogueTypes.h

USTRUCT(BlueprintType)
struct FDialogueClientState
{
    GENERATED_BODY()

    // Routes this state to the correct in-progress session on the client.
    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    // Speaker identity tag. Client UI resolves display name and portrait from its own registry.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag SpeakerTag;

    // Localized line text. Empty for a choice node that follows a non-ACK line.
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
    // Client adds local elapsed time to drive the countdown UI without per-tick RPCs.
    UPROPERTY(BlueprintReadOnly)
    float TimeoutRemainingSeconds = 0.0f;

    // The UDialogueComponent that owns this session.
    // Carried in client state so UDialogueManagerComponent::SessionOwners is populated
    // automatically on ClientRPC_ReceiveDialogueState — no separate registration RPC needed.
    // UDialogueComponent must be replicated (SetIsReplicated(true) in constructor) so
    // the engine can resolve its NetGUID on the receiving client.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UDialogueComponent> OwnerComponent;
};
```

> **`OwnerComponent` and replication:** `UDialogueComponent` must call `SetIsReplicated(true)` in its constructor. Without this the engine cannot assign a `NetGUID` to the component, and the `OwnerComponent` pointer will be null on the client when the RPC is deserialized. This is the canonical UE5 approach for passing component object references through RPCs.

---

## FDialogueEventMessage

GMS payload broadcast by `UDialogueNode_Event`.

```cpp
// File: Dialogue/DialogueTypes.h

USTRUCT(BlueprintType)
struct FDialogueEventMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    // Chooser or sole instigator at the time the event fired.
    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> Instigator;

    // Game-specific payload. Meaning is event-type-specific.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag PayloadTag;
};
```
