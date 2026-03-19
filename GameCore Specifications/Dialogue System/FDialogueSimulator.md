# FDialogueSimulator

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)  
**File:** `Dialogue/DialogueSimulator.h / .cpp`

---

## Overview

`FDialogueSimulator` is the core interpreter. It is a plain C++ struct — no `UObject` lifecycle, no tick, no world reference. It takes a `FDialogueSession&` and advances it one logical step at a time.

The server's `UDialogueComponent` calls `FDialogueSimulator::Advance()` to run the session. The editor preview tool (`SDialoguePreviewWidget`) also calls `Advance()` directly on a locally owned `FDialogueSession` — no world, no RPCs, no replication required. This is the key design constraint: **zero duplication of interpreter logic**.

---

## Class Definition

```cpp
// File: Dialogue/DialogueSimulator.h

struct GAMECORE_API FDialogueSimulator
{
    // Maximum number of consecutive Continue steps before aborting with AssetError.
    // Guards against infinite loops in malformed assets.
    static constexpr int32 MaxAutoSteps = 256;

    // Advance the session until it reaches a Wait or End state, or MaxAutoSteps is hit.
    // Returns the final FDialogueStepResult that caused the advance to stop.
    // The caller (UDialogueComponent or editor tool) inspects the result and acts accordingly.
    static FDialogueStepResult Advance(FDialogueSession& Session);

    // Submit a choice index into a waiting session.
    // Validates the index and lock condition. Returns the target node index, or INDEX_NONE on failure.
    // Caller must call Advance() again after a successful ResolveChoice.
    static int32 ResolveChoice(FDialogueSession& Session, int32 ChoiceIndex);

    // Pop the current stack frame and resume the parent frame at its ReturnNodeIndex.
    // Called by Advance internally when a sub-asset ends.
    static bool PopStack(FDialogueSession& Session);

private:
    // Execute one node and return its result. Handles null/OOB node indices.
    static FDialogueStepResult ExecuteNode(FDialogueSession& Session);

    // Build a FRequirementContext from the session's Chooser actor and Variables map.
    static FRequirementContext BuildContext(const FDialogueSession& Session);
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
            // Validate next node index before advancing.
            if (Result.NextNode == INDEX_NONE)
            {
                // Reached end of asset with no explicit End node.
                // Treat as graceful completion.
                if (Session.AssetStack.Num() > 1)
                {
                    // Pop sub-dialogue and continue in parent.
                    PopStack(Session);
                    break; // continue while loop in parent frame
                }
                Result.Action    = EDialogueStepAction::EndSession;
                Result.EndReason = EDialogueEndReason::Completed;
                return Result;
            }
            Session.CurrentFrame().CurrentNodeIndex = Result.NextNode;
            break;

        case EDialogueStepAction::SubDialoguePush:
            // Store return address on current frame before pushing.
            Session.CurrentFrame().ReturnNodeIndex = Result.NextNode;
            {
                FDialogueStackFrame NewFrame;
                NewFrame.Asset            = Result.SubAsset;
                NewFrame.CurrentNodeIndex = Result.SubAsset->StartNodeIndex;
                NewFrame.ReturnNodeIndex  = INDEX_NONE;
                Session.AssetStack.Push(NewFrame);
            }
            break;

        case EDialogueStepAction::WaitForACK:
        case EDialogueStepAction::WaitForChoice:
            // Store where we will resume after the wait.
            Session.CurrentFrame().CurrentNodeIndex = Result.NextNode;
            Session.bWaiting = true;
            return Result; // Caller pushes FDialogueClientState to clients.

        case EDialogueStepAction::EndSession:
            return Result;
        }
    }

    // Exceeded MaxAutoSteps — asset is likely malformed (infinite loop).
    UE_LOG(LogDialogue, Error, TEXT("FDialogueSimulator::Advance exceeded MaxAutoSteps. Asset: %s"),
        *Session.CurrentFrame().Asset->GetName());
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
    const UDialogueNode* Node = Session.CurrentFrame().Asset->GetNode(
        Session.CurrentFrame().CurrentNodeIndex - 1 // CurrentNodeIndex is post-advance; rewind
    );
    // Note: UDialogueComponent stores the waiting node index explicitly to avoid this arithmetic.
    // See UDialogueComponent::PendingChoiceNodeIndex.

    const UDialogueNode_PlayerChoice* ChoiceNode = Cast<UDialogueNode_PlayerChoice>(Node);
    if (!ChoiceNode)
    {
        UE_LOG(LogDialogue, Warning, TEXT("ResolveChoice called but current node is not a PlayerChoice."));
        return INDEX_NONE;
    }

    if (!ChoiceNode->Choices.IsValidIndex(ChoiceIndex))
    {
        UE_LOG(LogDialogue, Warning, TEXT("ResolveChoice: ChoiceIndex %d out of range."), ChoiceIndex);
        return INDEX_NONE;
    }

    const FDialogueChoiceConfig& Choice = ChoiceNode->Choices[ChoiceIndex];

    // Re-validate lock condition server-side. Never trust client state.
    if (Choice.LockCondition)
    {
        FRequirementContext Ctx = BuildContext(Session);
        if (!URequirementLibrary::EvaluateRequirement(Choice.LockCondition, Ctx).bPassed)
        {
            UE_LOG(LogDialogue, Warning, TEXT("ResolveChoice: locked choice %d submitted — possible exploit attempt."), ChoiceIndex);
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
    Session.CurrentFrame().CurrentNodeIndex = ReturnNode;
    return true;
}
```

---

## Editor Usage

The editor preview tool creates a `FDialogueSession` directly, populates `AssetStack` with the root asset, and calls `FDialogueSimulator::Advance()` and `FDialogueSimulator::ResolveChoice()` without any world or network context. See [Editor Preview Tool](Editor%20Preview%20Tool.md) for the full widget implementation.

```cpp
// Minimal editor bootstrap:
FDialogueSession SimSession;
FDialogueStackFrame RootFrame;
RootFrame.Asset            = MyDialogueAsset;
RootFrame.CurrentNodeIndex = MyDialogueAsset->StartNodeIndex;
RootFrame.ReturnNodeIndex  = INDEX_NONE;
SimSession.AssetStack.Add(RootFrame);
SimSession.SessionID = FGuid::NewGuid();
// Chooser is left invalid for the simulator — Condition nodes receive an empty context.

FDialogueStepResult Result = FDialogueSimulator::Advance(SimSession);
// Inspect Result.Action to render the correct UI state.
```
