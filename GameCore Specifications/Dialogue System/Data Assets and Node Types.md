# Data Assets and Node Types

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)

---

## UDialogueAsset

**File:** `Dialogue/Assets/DialogueAsset.h / .cpp`  
**Base:** `UDataAsset`

The authored content unit. A flat array of instanced `UDialogueNode` objects and a start index. Shared across all sessions that use it — never modified at runtime.

```cpp
// File: Dialogue/Assets/DialogueAsset.h

UCLASS()
class GAMECORE_API UDialogueAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // Session mode. Single wraps the instigator in a one-element array internally.
    // Designers set this once per asset.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    EDialogueSessionMode SessionMode = EDialogueSessionMode::Single;

    // Index of the first node executed when a session starts.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    int32 StartNodeIndex = 0;

    // All nodes in this dialogue. Instanced — each is a UDialogueNode subclass
    // configured directly in this asset. Indices are stable after cook.
    UPROPERTY(EditAnywhere, Instanced, Category = "Dialogue")
    TArray<TObjectPtr<UDialogueNode>> Nodes;

    // Returns nullptr if Index is out of bounds. Always check return value.
    const UDialogueNode* GetNode(int32 Index) const;

#if WITH_EDITOR
    // Called on asset save. Logs a warning for every UDialogueNode_Line
    // whose LineText is not sourced from a StringTable.
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
```

> **Note:** `IsDataValid` must be implemented. Raw `FText` literals in line nodes are a localization bug — the validation pass catches them at save time rather than at runtime.

---

## EDialogueSessionMode

```cpp
// File: Dialogue/DialogueEnums.h

UENUM(BlueprintType)
enum class EDialogueSessionMode : uint8
{
    // One instigator. Direct replication to that actor's UDialogueManagerComponent.
    Single  UMETA(DisplayName = "Single"),
    // Multiple participants. One Chooser (choice submissions accepted),
    // rest are Observers (state replicated, submissions rejected).
    Group   UMETA(DisplayName = "Group"),
};

UENUM(BlueprintType)
enum class EDialogueEndReason : uint8
{
    Completed               UMETA(DisplayName = "Completed"),           // Reached UDialogueNode_End
    Interrupted             UMETA(DisplayName = "Interrupted"),         // ForceEnd called externally
    ChooserDisconnected     UMETA(DisplayName = "Chooser Disconnected"), // Chooser left the session
    AssetError              UMETA(DisplayName = "Asset Error"),          // Invalid node index or null node
};
```

---

## UDialogueNode — Abstract Base

**File:** `Dialogue/Nodes/DialogueNode.h`  
**Base:** `UObject`

```cpp
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UDialogueNode : public UObject
{
    GENERATED_BODY()

public:
    // Execute this node against the current session.
    // Returns a FDialogueStepResult describing what the interpreter should do next.
    // Must be pure with respect to the node's own fields — all mutable state
    // lives in Session, never on the node itself.
    virtual FDialogueStepResult Execute(FDialogueSession& Session) const PURE_VIRTUAL(UDialogueNode::Execute, return {};);

#if WITH_EDITOR
    // Optional: editor display name shown in the preview tool log.
    virtual FString GetPreviewLabel() const { return GetClass()->GetName(); }
#endif
};
```

### FDialogueStepResult

```cpp
// File: Dialogue/DialogueTypes.h

UENUM()
enum class EDialogueStepAction : uint8
{
    Continue,       // Advance immediately to NextNodeIndex.
    WaitForACK,     // Push FDialogueClientState to clients; wait for ServerRPC_AcknowledgeLine.
    WaitForChoice,  // Push FDialogueClientState with choices; wait for ServerRPC_SubmitChoice.
    EndSession,     // Session is complete. EDialogueEndReason is in the result.
    SubDialoguePush,// Push a new asset onto the session stack.
};

struct FDialogueStepResult
{
    EDialogueStepAction Action    = EDialogueStepAction::Continue;
    int32               NextNode  = INDEX_NONE;  // For Continue and SubDialoguePush return node
    EDialogueEndReason  EndReason = EDialogueEndReason::Completed;
    // Populated by SubDialoguePush nodes:
    TObjectPtr<UDialogueAsset> SubAsset = nullptr;
};
```

---

## Node Types

### UDialogueNode_Line

Displays a single line of dialogue from a speaker. Optionally waits for client ACK before advancing.

```cpp
UCLASS(meta = (DisplayName = "Line"))
class GAMECORE_API UDialogueNode_Line : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Tag identifying the speaker. Client UI resolves display name and portrait from its registry.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line", meta = (Categories = "Dialogue.Speaker"))
    FGameplayTag SpeakerTag;

    // Localized line text. MUST be a StringTable reference — validated on asset save.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
    FText LineText;

    // Soft reference to a voice-over cue. Nullable. Client loads and plays locally.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
    TSoftObjectPtr<USoundBase> VoiceCue;

    // If true, the interpreter waits for ServerRPC_AcknowledgeLine before advancing.
    // If false, the server advances immediately after pushing state to clients (cutscene lines).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
    bool bRequiresAck = true;

    // Index of the next node. INDEX_NONE = end session after this line.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
    int32 NextNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;
};

// Implementation:
FDialogueStepResult UDialogueNode_Line::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    // Populate client state — done by UDialogueComponent after receiving the result.
    Result.Action   = bRequiresAck ? EDialogueStepAction::WaitForACK : EDialogueStepAction::Continue;
    Result.NextNode = NextNodeIndex;
    return Result;
}
```

---

### UDialogueNode_PlayerChoice

Presents a list of choices to the Chooser. The session waits until `ServerRPC_SubmitChoice` arrives or the timeout expires.

```cpp
USTRUCT(BlueprintType)
struct FDialogueChoiceConfig
{
    GENERATED_BODY()

    // Label shown to the player. MUST be a StringTable reference.
    UPROPERTY(EditAnywhere)
    FText Label;

    // Optional lock condition. If set and fails, the choice is shown as locked.
    // Evaluated server-side at execution time and again on RPC receipt.
    UPROPERTY(EditAnywhere, Instanced)
    TObjectPtr<URequirement_Composite> LockCondition;

    // Failure reason shown when locked. MUST be a StringTable reference if LockCondition is set.
    UPROPERTY(EditAnywhere)
    FText LockReasonText;

    // Node to advance to when this choice is selected.
    UPROPERTY(EditAnywhere)
    int32 TargetNodeIndex = INDEX_NONE;
};

UCLASS(meta = (DisplayName = "Player Choice"))
class GAMECORE_API UDialogueNode_PlayerChoice : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice")
    TArray<FDialogueChoiceConfig> Choices;

    // 0 = no timeout. When > 0, server auto-submits DefaultChoiceIndex on expiry.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice", meta = (ClampMin = "0.0"))
    float TimeoutSeconds = 0.0f;

    // Index into Choices selected automatically on timeout.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice")
    int32 DefaultChoiceIndex = 0;

    // In Group sessions: whether Observers see choices as read-only.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice")
    bool bShowChoicesToObservers = false;

    // Called by the interpreter. Builds FDialogueClientState choices and waits.
    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;

    // Called by UDialogueComponent after a valid choice RPC arrives.
    // Returns the TargetNodeIndex for the selected choice.
    int32 ResolveChoice(int32 ChoiceIndex, const FRequirementContext& Context) const;
};

// Execute builds client-facing choice list (evaluating locks) and returns WaitForChoice.
// ResolveChoice re-validates the lock condition on the server before returning the target.
```

> **Anti-cheat note:** `ResolveChoice` re-evaluates `LockCondition` on the server regardless of what the client reported. A client that spoofs a locked choice index receives an `INDEX_NONE` return, and the component logs a warning and ignores the RPC.

---

### UDialogueNode_Condition

Evaluates a requirement server-side and branches — no client involvement.

```cpp
UCLASS(meta = (DisplayName = "Condition Branch"))
class GAMECORE_API UDialogueNode_Condition : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Evaluated against the session instigator (or Chooser in group sessions).
    UPROPERTY(EditAnywhere, Instanced, Category = "Condition")
    TObjectPtr<URequirement_Composite> Condition;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
    int32 TrueNodeIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
    int32 FalseNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;
};

FDialogueStepResult UDialogueNode_Condition::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    Result.Action = EDialogueStepAction::Continue;
    if (Condition)
    {
        FRequirementContext Ctx = BuildContextFromSession(Session);
        const bool bPassed = URequirementLibrary::EvaluateRequirement(Condition, Ctx).bPassed;
        Result.NextNode = bPassed ? TrueNodeIndex : FalseNodeIndex;
    }
    else
    {
        UE_LOG(LogDialogue, Warning, TEXT("UDialogueNode_Condition has no Condition set — taking True branch."));
        Result.NextNode = TrueNodeIndex;
    }
    return Result;
}
```

---

### UDialogueNode_Event

Broadcasts a GMS message. The server advances immediately — no client involvement.

```cpp
UCLASS(meta = (DisplayName = "Event"))
class GAMECORE_API UDialogueNode_Event : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Tag identifying the event channel. Game code registers GMS listeners on these tags.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event", meta = (Categories = "DialogueEvent"))
    FGameplayTag EventTag;

    // Optional tag payload passed in the GMS message struct.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
    FGameplayTag PayloadTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
    int32 NextNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;
};

FDialogueStepResult UDialogueNode_Event::Execute(FDialogueSession& Session) const
{
    if (EventTag.IsValid())
    {
        FDialogueEventMessage Msg;
        Msg.SessionID    = Session.SessionID;
        Msg.Instigator   = Session.GetChooser();
        Msg.PayloadTag   = PayloadTag;
        UGameplayMessageSubsystem::Get(Session.GetWorld()).BroadcastMessage(EventTag, Msg);
    }
    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::Continue;
    Result.NextNode = NextNodeIndex;
    return Result;
}
```

---

### UDialogueNode_SetVariable

Writes a named value into the session variable map. No client involvement.

```cpp
UCLASS(meta = (DisplayName = "Set Variable"))
class GAMECORE_API UDialogueNode_SetVariable : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variable")
    FName VariableName;

    // Value to write. FDialogueVariant holds bool, int32, or FGameplayTag (see Runtime Structs).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variable")
    FDialogueVariant Value;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variable")
    int32 NextNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;
};

FDialogueStepResult UDialogueNode_SetVariable::Execute(FDialogueSession& Session) const
{
    if (!VariableName.IsNone())
        Session.Variables.Add(VariableName, Value);
    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::Continue;
    Result.NextNode = NextNodeIndex;
    return Result;
}
```

---

### UDialogueNode_SubDialogue

Pushes a nested `UDialogueAsset` onto the session stack. When the sub-asset ends normally, execution resumes at `ReturnNodeIndex` in the parent asset.

```cpp
UCLASS(meta = (DisplayName = "Sub-Dialogue"))
class GAMECORE_API UDialogueNode_SubDialogue : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SubDialogue")
    TObjectPtr<UDialogueAsset> SubAsset;

    // Node in the *parent* asset to return to when the sub-dialogue ends.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SubDialogue")
    int32 ReturnNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;
};

FDialogueStepResult UDialogueNode_SubDialogue::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    if (!SubAsset)
    {
        UE_LOG(LogDialogue, Error, TEXT("UDialogueNode_SubDialogue: SubAsset is null."));
        Result.Action   = EDialogueStepAction::Continue;
        Result.NextNode = ReturnNodeIndex; // skip gracefully
        return Result;
    }
    // The session stack push is handled by FDialogueSimulator on SubDialoguePush action.
    // ReturnNodeIndex is stored on the stack frame before pushing.
    Result.Action   = EDialogueStepAction::SubDialoguePush;
    Result.SubAsset = SubAsset;
    Result.NextNode = ReturnNodeIndex; // stored as the return address on the current frame
    return Result;
}
```

---

### UDialogueNode_Jump

Unconditional jump to any node index. Handles loops and path convergence.

```cpp
UCLASS(meta = (DisplayName = "Jump"))
class GAMECORE_API UDialogueNode_Jump : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump")
    int32 TargetNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;
};

FDialogueStepResult UDialogueNode_Jump::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::Continue;
    Result.NextNode = TargetNodeIndex;
    return Result;
}
```

> **Infinite loop guard:** `FDialogueSimulator::Step()` tracks the number of consecutive `Continue` actions in a single advance call and aborts with `AssetError` if it exceeds a configurable `MaxAutoSteps` threshold (default 256).

---

### UDialogueNode_End

Terminates the session. Optional reason tag for analytics.

```cpp
UCLASS(meta = (DisplayName = "End"))
class GAMECORE_API UDialogueNode_End : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Optional tag sent in the OnDialogueEnded broadcast and the GMS event.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "End", meta = (Categories = "DialogueEvent.End"))
    FGameplayTag EndReasonTag;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;
};

FDialogueStepResult UDialogueNode_End::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    Result.Action    = EDialogueStepAction::EndSession;
    Result.EndReason = EDialogueEndReason::Completed;
    return Result;
}
```
