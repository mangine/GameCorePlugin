# FDialogueSimulator

**File:** `Dialogue/DialogueSimulator.h / .cpp`

---

## Overview

The core interpreter for the Dialogue System. A plain C++ struct — no `UObject` lifecycle, no tick, no world reference.

`FDialogueSimulator::Advance()` takes a `FDialogueSession&` and runs the session until it reaches a `Wait` or `End` state, or the `MaxAutoSteps` safety limit. Both the server runtime (`UDialogueComponent`) and the editor preview tool (`FDialoguePreviewContext`) drive the simulator directly — zero duplication of interpreter logic.

Log category is defined in `DialogueSimulator.cpp`:
```cpp
DEFINE_LOG_CATEGORY(LogDialogue);
```

---

## Class Definition

```cpp
// File: Dialogue/DialogueSimulator.h

struct GAMECORE_API FDialogueSimulator
{
    // Maximum consecutive auto-advancing steps before aborting with AssetError.
    // Prevents infinite loops in malformed assets (Jump nodes cycling back to themselves).
    static constexpr int32 MaxAutoSteps = 256;

    // Advance the session until it reaches a Wait or End state, or MaxAutoSteps is hit.
    // INVARIANT: on return, Session.CurrentFrame().CurrentNodeIndex points at:
    //   - the waiting node (WaitForACK / WaitForChoice), or
    //   - INDEX_NONE (EndSession / empty stack).
    static FDialogueStepResult Advance(FDialogueSession& Session);

    // Resolve a submitted choice index against the currently-waiting PlayerChoice node.
    // CurrentNodeIndex points at the waiting node — no arithmetic required.
    // Re-validates the lock condition server-side (anti-cheat).
    // Returns the TargetNodeIndex on success, INDEX_NONE on any failure.
    // Caller must set CurrentNodeIndex to the returned value and call Advance() again.
    static int32 ResolveChoice(FDialogueSession& Session, int32 ChoiceIndex);

    // Pop the current stack frame and restore parent frame's ReturnNodeIndex.
    // Returns false if the stack has only one frame (cannot pop root).
    static bool PopStack(FDialogueSession& Session);

    // Build a FRequirementContext from the session's Chooser actor and Variables map.
    // Safe with a null Chooser (editor preview path) — returns a context with only
    // Variables-derived data and ContextTags from the EditorChooserProxy (if set).
    static FRequirementContext BuildContext(const FDialogueSession& Session);

private:
    // Execute the node at Session.CurrentFrame().CurrentNodeIndex.
    // Returns AssetError on out-of-bounds or null node.
    static FDialogueStepResult ExecuteNode(FDialogueSession& Session);
};
```

---

## Advance() — Implementation

```cpp
FDialogueStepResult FDialogueSimulator::Advance(FDialogueSession& Session)
{
    int32 Steps = 0;
    FDialogueStepResult Result;

    while (Steps++ < MaxAutoSteps)
    {
        Result = ExecuteNode(Session);

        switch (Result.Action)
        {
        case EDialogueStepAction::Continue:
        {
            if (Result.NextNode == INDEX_NONE)
            {
                // Node has no successor. Check if we can pop the stack.
                if (Session.AssetStack.Num() > 1)
                {
                    PopStack(Session);
                    break; // resume while-loop in parent frame
                }
                // Root frame exhausted without an explicit End node — graceful completion.
                Result.Action    = EDialogueStepAction::EndSession;
                Result.EndReason = EDialogueEndReason::Completed;
                return Result;
            }
            // Advance CurrentNodeIndex ONLY on Continue.
            // On WaitForACK/WaitForChoice, CurrentNodeIndex stays at the waiting node
            // so ResolveChoice and Server_ReceiveACK can find it without secondary storage.
            Session.CurrentFrame().CurrentNodeIndex = Result.NextNode;
            break;
        }

        case EDialogueStepAction::SubDialoguePush:
        {
            if (!Result.SubAsset)
            {
                FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
                    TEXT("DialogueSimulator"),
                    FString::Printf(
                        TEXT("SubDialoguePush with null SubAsset in asset '%s' at node %d."),
                        *Session.CurrentFrame().Asset->GetName(),
                        Session.CurrentFrame().CurrentNodeIndex));
                Result.Action    = EDialogueStepAction::EndSession;
                Result.EndReason = EDialogueEndReason::AssetError;
                return Result;
            }
            // Store the return address on the current frame before pushing.
            Session.CurrentFrame().ReturnNodeIndex = Result.NextNode;

            FDialogueStackFrame NewFrame;
            NewFrame.Asset            = Result.SubAsset;
            NewFrame.CurrentNodeIndex = Result.SubAsset->StartNodeIndex;
            NewFrame.ReturnNodeIndex  = INDEX_NONE;
            Session.AssetStack.Push(NewFrame);
            break;
        }

        case EDialogueStepAction::WaitForACK:
        case EDialogueStepAction::WaitForChoice:
            // CurrentNodeIndex is NOT advanced here — it remains at the waiting node.
            Session.bWaiting = true;
            return Result;

        case EDialogueStepAction::EndSession:
            return Result;
        }
    }

    FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
        TEXT("DialogueSimulator"),
        FString::Printf(
            TEXT("Advance exceeded MaxAutoSteps (%d) in asset '%s'. Possible infinite loop."),
            MaxAutoSteps,
            *Session.CurrentFrame().Asset->GetName()));

    Result.Action    = EDialogueStepAction::EndSession;
    Result.EndReason = EDialogueEndReason::AssetError;
    return Result;
}
```

---

## ResolveChoice() — Implementation

```cpp
int32 FDialogueSimulator::ResolveChoice(FDialogueSession& Session, int32 ChoiceIndex)
{
    // CurrentNodeIndex points at the waiting PlayerChoice node — no arithmetic needed.
    const UDialogueNode* RawNode =
        Session.CurrentFrame().Asset->GetNode(Session.CurrentFrame().CurrentNodeIndex);

    const UDialogueNode_PlayerChoice* ChoiceNode =
        Cast<UDialogueNode_PlayerChoice>(RawNode);

    if (!ChoiceNode)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
            TEXT("DialogueSimulator"),
            TEXT("ResolveChoice called but CurrentNodeIndex does not point at a PlayerChoice node."));
        return INDEX_NONE;
    }

    if (!ChoiceNode->Choices.IsValidIndex(ChoiceIndex))
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
            TEXT("DialogueSimulator"),
            FString::Printf(
                TEXT("ResolveChoice: ChoiceIndex %d out of range (%d choices available)."),
                ChoiceIndex, ChoiceNode->Choices.Num()));
        return INDEX_NONE;
    }

    const FDialogueChoiceConfig& Choice = ChoiceNode->Choices[ChoiceIndex];

    // Re-validate lock condition server-side. Never trust client-reported availability.
    if (Choice.LockCondition)
    {
        FRequirementContext Ctx = BuildContext(Session);
        const FRequirementResult EvalResult =
            URequirementLibrary::EvaluateRequirement(Choice.LockCondition, Ctx);

        if (!EvalResult.bPassed)
        {
            FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
                TEXT("DialogueSimulator"),
                FString::Printf(
                    TEXT("ResolveChoice: choice %d is locked (requirement failed). "
                         "Possible exploit attempt by actor '%s'."),
                    ChoiceIndex,
                    Session.GetChooser()
                        ? *Session.GetChooser()->GetName()
                        : TEXT("<null chooser>")));
            return INDEX_NONE;
        }
    }

    Session.bWaiting = false;
    return Choice.TargetNodeIndex;
}
```

---

## PopStack() — Implementation

```cpp
bool FDialogueSimulator::PopStack(FDialogueSession& Session)
{
    if (Session.AssetStack.Num() <= 1)
        return false; // Cannot pop root frame.

    const int32 ReturnNode = Session.CurrentFrame().ReturnNodeIndex;
    Session.AssetStack.Pop();
    // Restore the parent frame to the stored return address.
    Session.CurrentFrame().CurrentNodeIndex = ReturnNode;
    return true;
}
```

---

## BuildContext() — Implementation

Converts session state into a `FRequirementContext` for condition and choice-lock evaluation. Safe with a null Chooser (editor preview path).

```cpp
FRequirementContext FDialogueSimulator::BuildContext(const FDialogueSession& Session)
{
    FRequirementContext Ctx;

    if (AActor* Chooser = Session.GetChooser())
    {
        Ctx.SourceActor = Chooser;
        // Populate source tags from ITaggedInterface if the Chooser implements it.
        if (const ITaggedInterface* Tagged = Cast<ITaggedInterface>(Chooser))
            Ctx.SourceTags = Tagged->GetOwnedTags();
    }

    // Convert session variables into FRequirementContext data.
    // Tag variants are merged into SourceTags (for URequirement_HasTag evaluation).
    // Bool and Int variants are stored as named payloads in PersistedData.
    for (const TTuple<FName, FDialogueVariant>& Pair : Session.Variables)
    {
        switch (Pair.Value.Type)
        {
        case EDialogueVariantType::Tag:
            if (Pair.Value.TagValue.IsValid())
                Ctx.SourceTags.AddTag(Pair.Value.TagValue);
            break;

        case EDialogueVariantType::Bool:
            Ctx.PersistedData.Add(Pair.Key,
                FRequirementPayload{ static_cast<int32>(Pair.Value.BoolValue ? 1 : 0) });
            break;

        case EDialogueVariantType::Int:
            Ctx.PersistedData.Add(Pair.Key,
                FRequirementPayload{ Pair.Value.IntValue });
            break;
        }
    }

    return Ctx;
}
```

---

## ExecuteNode() — Implementation

```cpp
FDialogueStepResult FDialogueSimulator::ExecuteNode(FDialogueSession& Session)
{
    const FDialogueStackFrame& Frame = Session.CurrentFrame();

    if (!Frame.Asset)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
            TEXT("DialogueSimulator"),
            TEXT("ExecuteNode: current stack frame has a null Asset."));
        return { EDialogueStepAction::EndSession, INDEX_NONE, EDialogueEndReason::AssetError };
    }

    const UDialogueNode* Node = Frame.Asset->GetNode(Frame.CurrentNodeIndex);
    if (!Node)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
            TEXT("DialogueSimulator"),
            FString::Printf(
                TEXT("ExecuteNode: node index %d is out of bounds or null in asset '%s'."),
                Frame.CurrentNodeIndex, *Frame.Asset->GetName()));
        return { EDialogueStepAction::EndSession, INDEX_NONE, EDialogueEndReason::AssetError };
    }

    return Node->Execute(Session);
}
```

---

## Editor Bootstrap (no world required)

```cpp
// Used by FDialoguePreviewContext::Restart():
FDialogueSession SimSession;
FDialogueStackFrame RootFrame;
RootFrame.Asset            = MyDialogueAsset;
RootFrame.CurrentNodeIndex = MyDialogueAsset->StartNodeIndex;
RootFrame.ReturnNodeIndex  = INDEX_NONE;
SimSession.AssetStack.Add(RootFrame);
SimSession.SessionID = FGuid::NewGuid();
// Chooser is UDialogueEditorChooserProxy — returns ContextTags for requirement evaluation.

FDialogueStepResult Result = FDialogueSimulator::Advance(SimSession);
// Inspect Result.Action to render the correct preview widget state.
```
