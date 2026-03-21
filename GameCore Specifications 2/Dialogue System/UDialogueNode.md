# UDialogueNode (Abstract Base) and All Node Subclasses

**Files:**
- `Dialogue/Nodes/DialogueNode.h` — abstract base
- `Dialogue/Nodes/DialogueNode_Line.h / .cpp`
- `Dialogue/Nodes/DialogueNode_PlayerChoice.h / .cpp`
- `Dialogue/Nodes/DialogueNode_Condition.h / .cpp`
- `Dialogue/Nodes/DialogueNode_Event.h / .cpp`
- `Dialogue/Nodes/DialogueNode_SetVariable.h / .cpp`
- `Dialogue/Nodes/DialogueNode_SubDialogue.h / .cpp`
- `Dialogue/Nodes/DialogueNode_Jump.h / .cpp`
- `Dialogue/Nodes/DialogueNode_End.h / .cpp`

---

## UDialogueNode — Abstract Base

```cpp
// File: Dialogue/Nodes/DialogueNode.h

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
```

> **Critical constraint:** Nodes own no per-instance mutable state. They are shared read-only objects inside `UDialogueAsset`. Multiple simultaneous sessions may execute the same node object — any mutation would be a data race.

---

## UDialogueNode_Line

Displays a single line of dialogue from a speaker. Optionally waits for client ACK before advancing.

```cpp
// File: Dialogue/Nodes/DialogueNode_Line.h

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
};

// Implementation:
FDialogueStepResult UDialogueNode_Line::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    // Display data (SpeakerTag, LineText, VoiceCue) is read by UDialogueComponent::BuildClientState
    // from the node at CurrentNodeIndex. It is NOT carried in FDialogueStepResult.
    Result.Action   = bRequiresAck ? EDialogueStepAction::WaitForACK : EDialogueStepAction::Continue;
    Result.NextNode = NextNodeIndex;
    return Result;
}
```

> **Important:** For `bRequiresAck=false` (auto-advance) lines, `UDialogueComponent::RunSession` must call `PushClientState` **before** advancing `CurrentNodeIndex`. This is a known issue (KI-4) in the original spec — ensure the implementation handles it correctly.

---

## UDialogueNode_PlayerChoice

Presents a list of choices to the Chooser. The session waits until `Server_ReceiveChoice` arrives or timeout expires.

```cpp
// File: Dialogue/Nodes/DialogueNode_PlayerChoice.h

// Per-choice configuration. Embedded in the node asset.
USTRUCT(BlueprintType)
struct FDialogueChoiceConfig
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

    // Called by FDialogueSimulator::Advance. Builds FDialogueClientState choices
    // (evaluating locks) and returns WaitForChoice.
    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;
};

// Execute returns WaitForChoice — display data is read by BuildClientState from this node.
FDialogueStepResult UDialogueNode_PlayerChoice::Execute(FDialogueSession& Session) const
{
    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::WaitForChoice;
    Result.NextNode = INDEX_NONE; // Resolved by FDialogueSimulator::ResolveChoice
    return Result;
}
```

> **Anti-cheat:** `FDialogueSimulator::ResolveChoice` re-evaluates `LockCondition` server-side on every RPC receipt, regardless of what the client reported. A spoofed locked-choice submission receives `INDEX_NONE` and the session remains waiting.

---

## UDialogueNode_Condition

Evaluates a requirement server-side and branches — no client involvement, no client state push.

```cpp
// File: Dialogue/Nodes/DialogueNode_Condition.h

UCLASS(meta = (DisplayName = "Condition Branch"))
class GAMECORE_API UDialogueNode_Condition : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Evaluated against the session Chooser via FDialogueSimulator::BuildContext.
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
        // Use FDialogueSimulator::BuildContext — NOT a local BuildContextFromSession call.
        FRequirementContext Ctx = FDialogueSimulator::BuildContext(Session);
        const bool bPassed = URequirementLibrary::EvaluateRequirement(Condition, Ctx).bPassed;
        Result.NextNode = bPassed ? TrueNodeIndex : FalseNodeIndex;
    }
    else
    {
        UE_LOG(LogDialogue, Warning,
            TEXT("UDialogueNode_Condition has no Condition set — taking True branch."));
        Result.NextNode = TrueNodeIndex;
    }
    return Result;
}
```

---

## UDialogueNode_Event

Broadcasts a GMS message. Server advances immediately — no client involvement.

```cpp
// File: Dialogue/Nodes/DialogueNode_Event.h

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
};

FDialogueStepResult UDialogueNode_Event::Execute(FDialogueSession& Session) const
{
    if (EventTag.IsValid())
    {
        FDialogueEventMessage Msg;
        Msg.SessionID  = Session.SessionID;
        Msg.Instigator = Session.GetChooser();
        Msg.PayloadTag = PayloadTag;
        UGameplayMessageSubsystem::Get(Session.GetWorld())
            .BroadcastMessage(EventTag, Msg);
    }
    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::Continue;
    Result.NextNode = NextNodeIndex;
    return Result;
}
```

---

## UDialogueNode_SetVariable

Writes a named value into `FDialogueSession::Variables`. No client involvement.

```cpp
// File: Dialogue/Nodes/DialogueNode_SetVariable.h

UCLASS(meta = (DisplayName = "Set Variable"))
class GAMECORE_API UDialogueNode_SetVariable : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variable")
    FName VariableName;

    // Value to write. FDialogueVariant holds bool, int32, or FGameplayTag.
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

## UDialogueNode_SubDialogue

Pushes a nested `UDialogueAsset` onto the session stack. When the sub-asset ends normally, execution resumes at `ReturnNodeIndex` in the parent asset.

```cpp
// File: Dialogue/Nodes/DialogueNode_SubDialogue.h

UCLASS(meta = (DisplayName = "Sub-Dialogue"))
class GAMECORE_API UDialogueNode_SubDialogue : public UDialogueNode
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SubDialogue")
    TObjectPtr<UDialogueAsset> SubAsset;

    // Node in the *parent* asset to return to when the sub-dialogue ends normally.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SubDialogue")
    int32 ReturnNodeIndex = INDEX_NONE;

    virtual FDialogueStepResult Execute(FDialogueSession& Session) const override;
};

FDialogueStepResult UDialogueNode_SubDialogue::Execute(FDialogueSession& Session) const
{
    if (!SubAsset)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
            TEXT("DialogueSimulator"),
            TEXT("UDialogueNode_SubDialogue: SubAsset is null — skipping sub-dialogue."));
        // Graceful fallback: skip the sub-dialogue and return to ReturnNodeIndex.
        FDialogueStepResult Result;
        Result.Action   = EDialogueStepAction::Continue;
        Result.NextNode = ReturnNodeIndex;
        return Result;
    }
    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::SubDialoguePush;
    Result.SubAsset = SubAsset;
    // ReturnNodeIndex is stored as the return address on the current stack frame
    // before FDialogueSimulator pushes the new frame.
    Result.NextNode = ReturnNodeIndex;
    return Result;
}
```

---

## UDialogueNode_Jump

Unconditional jump to any node index. Handles loops and path convergence.

```cpp
// File: Dialogue/Nodes/DialogueNode_Jump.h

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

> **Infinite loop guard:** `FDialogueSimulator::Advance` aborts with `AssetError` after `MaxAutoSteps` (256) consecutive `Continue` actions — this catches Jump cycles.

---

## UDialogueNode_End

Terminates the session.

```cpp
// File: Dialogue/Nodes/DialogueNode_End.h

UCLASS(meta = (DisplayName = "End"))
class GAMECORE_API UDialogueNode_End : public UDialogueNode
{
    GENERATED_BODY()

public:
    // Optional tag sent in OnDialogueEnded broadcast and the GMS Session.Ended event.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "End",
        meta = (Categories = "DialogueEvent.End"))
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

> **EndReasonTag usage:** When `UDialogueComponent::EndSession` is called as a result of an `EndSession` step, it should read the `EndReasonTag` from the `UDialogueNode_End` at `CurrentNodeIndex` and include it in `FDialogueSessionEndedMessage`.
