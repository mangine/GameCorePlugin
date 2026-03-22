// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DialogueTypes.h"
#include "Requirements/RequirementContext.h"

// The core interpreter for the Dialogue System.
// A plain C++ struct — no UObject lifecycle, no tick, no world reference.
//
// Both UDialogueComponent (server runtime) and SDialoguePreviewWidget (editor)
// call Advance() directly — zero duplication of interpreter logic.
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
    // Variables-derived data and ContextTags from the Chooser (if it implements ITaggedInterface).
    //
    // DEVIATION: FRequirementContext in this codebase uses FInstancedStruct Data,
    // not SourceActor/SourceTags/PersistedData as in the spec. BuildContext packs
    // a FDialogueRequirementContext into Data. Requirements evaluating dialogue sessions
    // must cast Data to FDialogueRequirementContext.
    static FRequirementContext BuildContext(const FDialogueSession& Session);

private:
    // Execute the node at Session.CurrentFrame().CurrentNodeIndex.
    // Returns AssetError on out-of-bounds or null node.
    static FDialogueStepResult ExecuteNode(FDialogueSession& Session);
};
