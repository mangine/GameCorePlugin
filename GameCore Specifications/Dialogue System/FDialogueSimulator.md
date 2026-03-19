# FDialogueSimulator

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)  
**File:** `Dialogue/DialogueSimulator.h / .cpp`

---

## Overview

`FDialogueSimulator` is the core interpreter. It is a plain C++ struct — no `UObject` lifecycle, no tick, no world reference. It takes a `FDialogueSession&` and advances it one logical step at a time.

The server's `UDialogueComponent` calls `FDialogueSimulator::Advance()` to run the session. The editor preview tool (`SDialoguePreviewWidget`) also calls `Advance()` directly on a locally owned `FDialogueSession` — no world, no RPCs, no replication required. This is the key design constraint: **zero duplication of interpreter logic**.

---

## Log Category

```cpp
// File: Dialogue/DialogueTypes.h  (declared once, included everywhere in the module)
GAMECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDialogue, Log, All);

// File: Dialogue/DialogueSimulator.cpp  (defined once)
DEFINE_LOG_CATEGORY(LogDialogue);
```

All internal warning/error output from the simulator goes through `FGameCoreBackend::GetLogging` with the `TAG_Log_Dialogue` routing tag. `UE_LOG(LogDialogue, ...)` is reserved for editor/development builds where the backend is unavailable (e.g. in `FDialoguePreviewContext`). In shipping server builds, use the backend exclusively.

```cpp
// Pattern used throughout the Dialogue module for server-side logging:
FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogWarning(
    TEXT("DialogueSimulator"), Message);
```

Add the routing tag to the game module's backend setup:

```ini
; Config/Tags/GameCore.Dialogue.ini
+GameplayTagList=(Tag="Log.Dialogue",DevComment="Dialogue system log routing tag")
```

---

## Class Definition

```cpp
// File: Dialogue/DialogueSimulator.h

struct GAMECORE_API FDialogueSimulator
{
    // Maximum consecutive auto-advancing steps before aborting with AssetError.
    // Prevents infinite loops in malformed assets (e.g. Jump nodes cycling back to themselves).
    static constexpr int32 MaxAutoSteps = 256;

    // Advance the session until it reaches a Wait or End state, or MaxAutoSteps is hit.
    // INVARIANT: on return, Session.CurrentFrame().CurrentNodeIndex points at:
    //   - the waiting node (WaitForACK / WaitForChoice), or
    //   - INDEX_NONE (EndSession / empty stack).
    // The caller inspects Result.Action and dispatches accordingly.
    static FDialogueStepResult Advance(FDialogueSession& Session);

    // Resolve a submitted choice index against the currently-waiting PlayerChoice node.
    // Reads the node at Session.CurrentFrame().CurrentNodeIndex — no arithmetic required
    // because CurrentNodeIndex is never advanced past the waiting node.
    // Re-validates the lock condition server-side (anti-cheat).
    // Returns the TargetNodeIndex on success, INDEX_NONE on any failure.
    // Caller must advance CurrentNodeIndex to the returned value and call Advance() again.
    static int32 ResolveChoice(FDialogueSession& Session, int32 ChoiceIndex);

    // Pop the current stack frame and restore the parent frame's ReturnNodeIndex.
    // Returns false if the stack has only one frame (cannot pop root).
    static bool PopStack(FDialogueSession& Session);

    // Build a FRequirementContext from the session's Chooser actor and Variables map.
    // Safe to call with a null Chooser (editor preview path) — returns an empty context
    // with only the Variables-derived data present.
    static FRequirementContext BuildContext(const FDialogueSession& Session);

private:
    // Execute the node at Session.CurrentFrame().CurrentNodeIndex.
    // Returns a valid FDialogueStepResult or an AssetError result on OOB/null node.
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
                // Reached a node with no successor and no explicit End node.
                if (Session.AssetStack.Num() > 1)
                {
                    PopStack(Session);
                    break; // resume while-loop in parent frame
                }
                // Root frame exhausted — treat as graceful completion.
                Result.Action    = EDialogueStepAction::EndSession;
                Result.EndReason = EDialogueEndReason::Completed;
                return Result;
            }
            // Advance ONLY on Continue — CurrentNodeIndex must stay at the waiting node
            // for WaitForACK/WaitForChoice so that ResolveChoice can find it without
            // any secondary index storage.
            Session.CurrentFrame().CurrentNodeIndex = Result.NextNode;
            break;
        }

        case EDialogueStepAction::SubDialoguePush:
        {
            if (!Result.SubAsset)
            {
                FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
                    TEXT("DialogueSimulator"),
                    FString::Printf(TEXT("SubDialoguePush with null SubAsset in asset '%s' at node %d."),
                        *Session.CurrentFrame().Asset->GetName(),
                        Session.CurrentFrame().CurrentNodeIndex));
                Result.Action    = EDialogueStepAction::EndSession;
                Result.EndReason = EDialogueEndReason::AssetError;
                return Result;
            }
            // Store the return address on the current frame before pushing the new one.
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
            // CurrentNodeIndex is NOT advanced here.
            // It remains pointing at the waiting node so that:
            //   - Server_ReceiveACK resumes by reading CurrentNodeIndex → NextNode from the node.
            //   - ResolveChoice reads the PlayerChoice node directly at CurrentNodeIndex.
            Session.bWaiting = true;
            return Result;

        case EDialogueStepAction::EndSession:
            return Result;
        }
    }

    FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
        TEXT("DialogueSimulator"),
        FString::Printf(TEXT("Advance exceeded MaxAutoSteps (%d) in asset '%s'. Possible infinite loop."),
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
    const UDialogueNode* RawNode = Session.CurrentFrame().Asset
        ->GetNode(Session.CurrentFrame().CurrentNodeIndex);

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
            FString::Printf(TEXT("ResolveChoice: ChoiceIndex %d out of range (%d choices available)."),
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
    // Restore the parent frame to the return address stored before the push.
    Session.CurrentFrame().CurrentNodeIndex = ReturnNode;
    return true;
}
```

---

## BuildContext() — Implementation

Converts session state into a `FRequirementContext` for condition and choice-lock evaluation. Safe with a null Chooser (editor preview path).

`FDialogueVariant` values are converted inline — no `FRequirementPayload::FromVariant` helper is required on the Requirement System. The conversion is fully specified here:

```cpp
FRequirementContext FDialogueSimulator::BuildContext(const FDialogueSession& Session)
{
    FRequirementContext Ctx;

    if (AActor* Chooser = Session.GetChooser())
    {
        Ctx.SourceActor = Chooser;
        // Populate source tags from ITaggedInterface if implemented.
        if (const ITaggedInterface* Tagged = Cast<ITaggedInterface>(Chooser))
            Ctx.SourceTags = Tagged->GetOwnedTags();
    }

    // Convert session variables into FRequirementContext data.
    // Tag variants are merged into SourceTags so tag-based requirements evaluate correctly.
    // Bool and Int variants are stored as named payloads in PersistedData.
    for (const TTuple<FName, FDialogueVariant>& Pair : Session.Variables)
    {
        switch (Pair.Value.Type)
        {
        case EDialogueVariantType::Tag:
            // Inject Tag variables as source tags so URequirement_HasTag evaluates correctly.
            if (Pair.Value.TagValue.IsValid())
                Ctx.SourceTags.AddTag(Pair.Value.TagValue);
            break;

        case EDialogueVariantType::Bool:
            // Store as an int32 payload (0 or 1) under the variable name.
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

> **Note:** `FRequirementPayload` is assumed to have at minimum an `int32`-constructible form, consistent with the Requirement System spec. No changes to the Requirement System are required — the conversion is fully self-contained here.

---

## ExecuteNode() — Implementation

```cpp
FDialogueStepResult FDialogueSimulator::ExecuteNode(FDialogueSession& Session)
{
    const FDialogueStackFrame& Frame = Session.CurrentFrame();

    if (!Frame.Asset)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
            TEXT("DialogueSimulator"), TEXT("ExecuteNode: current stack frame has a null Asset."));
        return { EDialogueStepAction::EndSession, INDEX_NONE, EDialogueEndReason::AssetError };
    }

    const UDialogueNode* Node = Frame.Asset->GetNode(Frame.CurrentNodeIndex);
    if (!Node)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Dialogue)->LogError(
            TEXT("DialogueSimulator"),
            FString::Printf(TEXT("ExecuteNode: node index %d is out of bounds or null in asset '%s'."),
                Frame.CurrentNodeIndex, *Frame.Asset->GetName()));
        return { EDialogueStepAction::EndSession, INDEX_NONE, EDialogueEndReason::AssetError };
    }

    return Node->Execute(Session);
}
```

---

## Editor Bootstrap

The editor preview tool creates a `FDialogueSession` directly and drives it with `FDialogueSimulator`. No world, no RPCs.

```cpp
FDialogueSession SimSession;
FDialogueStackFrame RootFrame;
RootFrame.Asset            = MyDialogueAsset;
RootFrame.CurrentNodeIndex = MyDialogueAsset->StartNodeIndex;
RootFrame.ReturnNodeIndex  = INDEX_NONE;
SimSession.AssetStack.Add(RootFrame);
SimSession.SessionID = FGuid::NewGuid();
// Chooser is left null — Condition nodes receive an empty context.
// Designer populates FDialoguePreviewContext::ContextTags to simulate requirements.

FDialogueStepResult Result = FDialogueSimulator::Advance(SimSession);
// Inspect Result.Action to render the correct preview widget state.
```
