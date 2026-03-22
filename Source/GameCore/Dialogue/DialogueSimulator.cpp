// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueSimulator.h"

#include "DialogueTypes.h"
#include "Assets/DialogueAsset.h"
#include "Nodes/DialogueNode.h"
#include "Nodes/DialogueNode_PlayerChoice.h"
#include "Core/Backend/GameCoreBackend.h"
#include "Requirements/RequirementContext.h"
#include "Requirements/RequirementComposite.h"
#include "Tags/TaggedInterface.h"
#include "GameplayTagContainer.h"

// Log category used across the Dialogue system.
// Declared in DialogueTypes.h, defined here.
DEFINE_LOG_CATEGORY(LogDialogue);

// Gameplay tag for routing backend log calls.
// Defined as a local static so we avoid a static initialisation dependency on the tag manager.
static FGameplayTag GetLogDialogueTag()
{
    static FGameplayTag CachedTag;
    if (!CachedTag.IsValid())
        CachedTag = FGameplayTag::RequestGameplayTag(FName("Log.Dialogue"), /*bErrorIfNotFound=*/false);
    return CachedTag;
}

// ─────────────────────────────────────────────────────────────────────────────
// Advance
// ─────────────────────────────────────────────────────────────────────────────

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
                FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogError(
                    FString::Printf(
                        TEXT("DialogueSimulator: SubDialoguePush with null SubAsset in asset '%s' at node %d."),
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

    FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogError(
        FString::Printf(
            TEXT("DialogueSimulator: Advance exceeded MaxAutoSteps (%d) in asset '%s'. Possible infinite loop."),
            MaxAutoSteps,
            *Session.CurrentFrame().Asset->GetName()));

    Result.Action    = EDialogueStepAction::EndSession;
    Result.EndReason = EDialogueEndReason::AssetError;
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveChoice
// ─────────────────────────────────────────────────────────────────────────────

int32 FDialogueSimulator::ResolveChoice(FDialogueSession& Session, int32 ChoiceIndex)
{
    // CurrentNodeIndex points at the waiting PlayerChoice node — no arithmetic needed.
    const UDialogueNode* RawNode =
        Session.CurrentFrame().Asset->GetNode(Session.CurrentFrame().CurrentNodeIndex);

    const UDialogueNode_PlayerChoice* ChoiceNode =
        Cast<UDialogueNode_PlayerChoice>(RawNode);

    if (!ChoiceNode)
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogWarning(
            TEXT("DialogueSimulator: ResolveChoice called but CurrentNodeIndex does not point at a PlayerChoice node."));
        return INDEX_NONE;
    }

    if (!ChoiceNode->Choices.IsValidIndex(ChoiceIndex))
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogWarning(
            FString::Printf(
                TEXT("DialogueSimulator: ResolveChoice: ChoiceIndex %d out of range (%d choices available)."),
                ChoiceIndex, ChoiceNode->Choices.Num()));
        return INDEX_NONE;
    }

    const FDialogueChoiceConfig& Choice = ChoiceNode->Choices[ChoiceIndex];

    // Re-validate lock condition server-side. Never trust client-reported availability.
    if (Choice.LockCondition)
    {
        FRequirementContext Ctx = BuildContext(Session);
        const FRequirementResult EvalResult = Choice.LockCondition->Evaluate(Ctx);

        if (!EvalResult.bPassed)
        {
            FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogWarning(
                FString::Printf(
                    TEXT("DialogueSimulator: ResolveChoice: choice %d is locked (requirement failed). "
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

// ─────────────────────────────────────────────────────────────────────────────
// PopStack
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// BuildContext
// ─────────────────────────────────────────────────────────────────────────────

FRequirementContext FDialogueSimulator::BuildContext(const FDialogueSession& Session)
{
    // DEVIATION: The spec describes FRequirementContext with SourceActor, SourceTags,
    // and PersistedData fields. The actual FRequirementContext in this codebase uses
    // FInstancedStruct Data. We populate Data as a FDialogueRequirementContext.
    FDialogueRequirementContext DialogueCtx;
    DialogueCtx.ChooserActor = Session.GetChooser();

    if (const AActor* Chooser = Session.GetChooser())
    {
        // Populate source tags from ITaggedInterface if the Chooser implements it.
        if (const ITaggedInterface* Tagged = Cast<ITaggedInterface>(Chooser))
            DialogueCtx.SourceTags.AppendTags(Tagged->GetGameplayTags());
    }

    // Convert session variables into the context payload.
    // Tag variants are merged into SourceTags (for tag-based requirement evaluation).
    // Bool and Int variants are stored as named integers in NamedIntegers.
    for (const TTuple<FName, FDialogueVariant>& Pair : Session.Variables)
    {
        switch (Pair.Value.Type)
        {
        case EDialogueVariantType::Tag:
            if (Pair.Value.TagValue.IsValid())
                DialogueCtx.SourceTags.AddTag(Pair.Value.TagValue);
            break;

        case EDialogueVariantType::Bool:
            DialogueCtx.NamedIntegers.Add(Pair.Key,
                Pair.Value.BoolValue ? 1 : 0);
            break;

        case EDialogueVariantType::Int:
            DialogueCtx.NamedIntegers.Add(Pair.Key, Pair.Value.IntValue);
            break;
        }
    }

    return FRequirementContext::Make(DialogueCtx);
}

// ─────────────────────────────────────────────────────────────────────────────
// ExecuteNode
// ─────────────────────────────────────────────────────────────────────────────

FDialogueStepResult FDialogueSimulator::ExecuteNode(FDialogueSession& Session)
{
    const FDialogueStackFrame& Frame = Session.CurrentFrame();

    if (!Frame.Asset)
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogError(
            TEXT("DialogueSimulator: ExecuteNode: current stack frame has a null Asset."));
        return { EDialogueStepAction::EndSession, INDEX_NONE, EDialogueEndReason::AssetError };
    }

    const UDialogueNode* Node = Frame.Asset->GetNode(Frame.CurrentNodeIndex);
    if (!Node)
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogError(
            FString::Printf(
                TEXT("DialogueSimulator: ExecuteNode: node index %d is out of bounds or null in asset '%s'."),
                Frame.CurrentNodeIndex, *Frame.Asset->GetName()));
        return { EDialogueStepAction::EndSession, INDEX_NONE, EDialogueEndReason::AssetError };
    }

    return Node->Execute(Session);
}
