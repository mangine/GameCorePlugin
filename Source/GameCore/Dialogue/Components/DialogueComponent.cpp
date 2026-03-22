// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueComponent.h"

#include "DialogueManagerComponent.h"
#include "Dialogue/DialogueSimulator.h"
#include "Dialogue/Assets/DialogueAsset.h"
#include "Dialogue/Nodes/DialogueNode_Line.h"
#include "Dialogue/Nodes/DialogueNode_PlayerChoice.h"
#include "Dialogue/Nodes/DialogueNode_End.h"
#include "Core/Backend/GameCoreBackend.h"
#include "Requirements/RequirementComposite.h"
#include "GameplayMessageSubsystem.h"
#include "GameFramework/PlayerState.h"

// ─────────────────────────────────────────────────────────────────────────────
// Tag helpers
// ─────────────────────────────────────────────────────────────────────────────

static FGameplayTag GetLogDialogueTag()
{
    static FGameplayTag CachedTag;
    if (!CachedTag.IsValid())
        CachedTag = FGameplayTag::RequestGameplayTag(FName("Log.Dialogue"), false);
    return CachedTag;
}

static FGameplayTag GetSessionStartedTag()
{
    static FGameplayTag CachedTag;
    if (!CachedTag.IsValid())
        CachedTag = FGameplayTag::RequestGameplayTag(FName("DialogueEvent.Session.Started"), false);
    return CachedTag;
}

static FGameplayTag GetSessionEndedTag()
{
    static FGameplayTag CachedTag;
    if (!CachedTag.IsValid())
        CachedTag = FGameplayTag::RequestGameplayTag(FName("DialogueEvent.Session.Ended"), false);
    return CachedTag;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

UDialogueComponent::UDialogueComponent()
{
    // Must be true so the engine assigns a NetGUID to this component.
    // FDialogueClientState::OwnerComponent is an object reference in a ClientRPC;
    // without NetGUID the pointer will be null on the receiving client.
    SetIsReplicated(true);
    PrimaryComponentTick.bCanEverTick = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// UActorComponent overrides
// ─────────────────────────────────────────────────────────────────────────────

void UDialogueComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UDialogueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Force-end all sessions on shutdown.
    TArray<FGuid> SessionIDs;
    ActiveSessions.GetKeys(SessionIDs);
    for (const FGuid& ID : SessionIDs)
        EndSession(ID, EDialogueEndReason::Interrupted);

    Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Activation API
// ─────────────────────────────────────────────────────────────────────────────

void UDialogueComponent::StartDialogue(AActor* Instigator, UDialogueAsset* Asset)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    // Wrap the single instigator in an array and use the same group path.
    StartGroupDialogue({ Instigator }, Asset);
}

void UDialogueComponent::StartGroupDialogue(
    const TArray<AActor*>& Participants, UDialogueAsset* Asset)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    if (!Asset)
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogWarning(
            TEXT("StartGroupDialogue called with null Asset."));
        return;
    }
    if (Participants.IsEmpty())
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogWarning(
            TEXT("StartGroupDialogue called with empty Participants."));
        return;
    }

    for (AActor* P : Participants)
    {
        if (!P) continue;
        if (HasActiveSession(P))
        {
            if (bInterruptOnNewSession)
                ForceEndSession(GetSessionIDForInstigator(P));
            else
                return;
        }
    }

    FDialogueSession Session;
    Session.SessionID = FGuid::NewGuid();
    for (AActor* P : Participants)
        if (P) Session.Participants.Add(P);

    AActor* ResolvedChooser = ResolveChooser.IsBound()
        ? ResolveChooser.Execute(Participants)
        : Participants[0];

    if (!ResolvedChooser)
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogError(
            TEXT("StartGroupDialogue: ResolveChooser returned null. Session aborted."));
        return;
    }
    Session.Chooser = ResolvedChooser;

    FDialogueStackFrame RootFrame;
    RootFrame.Asset            = Asset;
    RootFrame.CurrentNodeIndex = Asset->StartNodeIndex;
    RootFrame.ReturnNodeIndex  = INDEX_NONE;
    Session.AssetStack.Add(RootFrame);

    const FGuid NewSessionID = Session.SessionID;
    ActiveSessions.Add(NewSessionID, MoveTemp(Session));
    OnDialogueStarted.Broadcast(NewSessionID, ResolvedChooser);

    // Broadcast GMS session started event.
    if (UWorld* World = GetWorld())
    {
        FDialogueSessionEventMessage StartMsg;
        StartMsg.SessionID  = NewSessionID;
        StartMsg.Instigator = ResolvedChooser;
        StartMsg.Asset      = Asset;
        UGameplayMessageSubsystem::Get(World)
            .BroadcastMessage(GetSessionStartedTag(), StartMsg);
    }

    RunSession(ActiveSessions[NewSessionID]);
}

void UDialogueComponent::ForceEndSession(FGuid SessionID)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    EndSession(SessionID, EDialogueEndReason::Interrupted);
}

bool UDialogueComponent::HasActiveSession(const AActor* Instigator) const
{
    if (!Instigator) return false;
    for (const auto& Pair : ActiveSessions)
    {
        const FDialogueSession& Session = Pair.Value;
        for (const TWeakObjectPtr<AActor>& P : Session.Participants)
        {
            if (P.Get() == Instigator)
                return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Flow
// ─────────────────────────────────────────────────────────────────────────────

void UDialogueComponent::RunSession(FDialogueSession& Session)
{
    // KI-4 fix: For auto-advance (bRequiresAck=false) lines, we need to push client state
    // before advancing. We detect this by checking if the result is Continue from a Line node.
    // The Line node returns Continue (not WaitForACK) for auto-advance lines.
    // We must push client state for those before advancing.

    const FDialogueStepResult Result = FDialogueSimulator::Advance(Session);

    switch (Result.Action)
    {
    case EDialogueStepAction::WaitForACK:
        // Always push client state before waiting — clients must see the line.
        PushClientState(Session, Result);
        break;

    case EDialogueStepAction::WaitForChoice:
    {
        PushClientState(Session, Result);

        // CurrentNodeIndex still points at the waiting PlayerChoice node.
        const UDialogueNode_PlayerChoice* ChoiceNode =
            Cast<UDialogueNode_PlayerChoice>(
                Session.CurrentFrame().Asset
                    ->GetNode(Session.CurrentFrame().CurrentNodeIndex));

        if (ChoiceNode && ChoiceNode->TimeoutSeconds > 0.0f)
        {
            StartTimeoutTimer(
                Session,
                ChoiceNode->TimeoutSeconds,
                ChoiceNode->DefaultChoiceIndex);
        }
        break;
    }

    case EDialogueStepAction::EndSession:
        EndSession(Session.SessionID, Result.EndReason);
        break;

    default:
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogError(
            FString::Printf(
                TEXT("RunSession: unexpected Result.Action %d returned from Advance."),
                static_cast<int32>(Result.Action)));
        EndSession(Session.SessionID, EDialogueEndReason::AssetError);
        break;
    }
}

FDialogueClientState UDialogueComponent::BuildClientState(
    const FDialogueSession& Session,
    const FDialogueStepResult& StepResult,
    bool bIsObserver) const
{
    FDialogueClientState State;
    State.SessionID      = Session.SessionID;
    State.bIsObserver    = bIsObserver;
    State.OwnerComponent = const_cast<UDialogueComponent*>(this);

    const UDialogueNode* WaitingNode =
        Session.CurrentFrame().Asset->GetNode(Session.CurrentFrame().CurrentNodeIndex);

    if (StepResult.Action == EDialogueStepAction::WaitForACK)
    {
        const UDialogueNode_Line* LineNode = Cast<UDialogueNode_Line>(WaitingNode);
        if (LineNode)
        {
            State.SpeakerTag     = LineNode->SpeakerTag;
            State.LineText       = LineNode->LineText;
            State.VoiceCue       = LineNode->VoiceCue;
            State.bWaitingForACK = true;
        }
    }
    else if (StepResult.Action == EDialogueStepAction::WaitForChoice)
    {
        const UDialogueNode_PlayerChoice* ChoiceNode =
            Cast<UDialogueNode_PlayerChoice>(WaitingNode);
        if (ChoiceNode)
        {
            State.bWaitingForChoice = true;
            State.TimeoutSeconds    = ChoiceNode->TimeoutSeconds;

            FRequirementContext Ctx = FDialogueSimulator::BuildContext(Session);
            for (int32 i = 0; i < ChoiceNode->Choices.Num(); ++i)
            {
                const FDialogueChoiceConfig& Cfg = ChoiceNode->Choices[i];
                FDialogueClientChoice ClientChoice;
                ClientChoice.ChoiceIndex = i;
                ClientChoice.Label       = Cfg.Label;
                if (Cfg.LockCondition)
                {
                    const bool bPassed = Cfg.LockCondition->Evaluate(Ctx).bPassed;
                    ClientChoice.bLocked       = !bPassed;
                    ClientChoice.LockReasonText = !bPassed ? Cfg.LockReasonText : FText::GetEmpty();
                }
                State.Choices.Add(ClientChoice);
            }

            // Set remaining timeout from the active timer if one is running.
            if (Session.TimeoutHandle.IsValid() && ChoiceNode->TimeoutSeconds > 0.0f)
            {
                if (UWorld* World = GetWorld())
                {
                    State.TimeoutRemainingSeconds =
                        World->GetTimerManager().GetTimerRemaining(Session.TimeoutHandle);
                }
            }
        }
    }

    return State;
}

void UDialogueComponent::PushClientState(
    FDialogueSession& Session, const FDialogueStepResult& StepResult)
{
    for (const TWeakObjectPtr<AActor>& ParticipantWeak : Session.Participants)
    {
        AActor* Participant = ParticipantWeak.Get();
        if (!Participant) continue;

        const bool bIsObserver = (Participant != Session.GetChooser());
        FDialogueClientState State = BuildClientState(Session, StepResult, bIsObserver);

        if (UDialogueManagerComponent* Manager = GetManagerComponent(Participant))
            Manager->ClientRPC_ReceiveDialogueState(State);
    }
}

void UDialogueComponent::EndSession(FGuid SessionID, EDialogueEndReason Reason)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session) return;

    if (Session->TimeoutHandle.IsValid())
    {
        if (UWorld* World = GetWorld())
            World->GetTimerManager().ClearTimer(Session->TimeoutHandle);
    }

    // Notify all participants.
    for (const TWeakObjectPtr<AActor>& ParticipantWeak : Session->Participants)
    {
        if (AActor* P = ParticipantWeak.Get())
        {
            if (UDialogueManagerComponent* Manager = GetManagerComponent(P))
                Manager->ClientRPC_DialogueEnded(SessionID, Reason);
        }
    }

    OnDialogueEnded.Broadcast(SessionID, Reason);

    // Broadcast GMS session ended event.
    if (UWorld* World = GetWorld())
    {
        FDialogueSessionEndedMessage EndMsg;
        EndMsg.SessionID  = SessionID;
        EndMsg.Instigator = Session->GetChooser();
        EndMsg.Reason     = Reason;
        // Read EndReasonTag from UDialogueNode_End if the session completed naturally.
        if (Reason == EDialogueEndReason::Completed && !Session->AssetStack.IsEmpty())
        {
            const UDialogueNode_End* EndNode = Cast<UDialogueNode_End>(
                Session->CurrentFrame().Asset
                    ->GetNode(Session->CurrentFrame().CurrentNodeIndex));
            if (EndNode)
                EndMsg.EndReasonTag = EndNode->EndReasonTag;
        }
        UGameplayMessageSubsystem::Get(World)
            .BroadcastMessage(GetSessionEndedTag(), EndMsg);
    }

    ActiveSessions.Remove(SessionID);
}

void UDialogueComponent::StartTimeoutTimer(
    FDialogueSession& Session, float TimeoutSeconds, int32 DefaultChoiceIndex)
{
    const FGuid CapturedID = Session.SessionID;
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            Session.TimeoutHandle,
            FTimerDelegate::CreateUObject(
                this, &UDialogueComponent::OnChoiceTimeout,
                CapturedID, DefaultChoiceIndex),
            TimeoutSeconds,
            /*bLoop=*/false);
    }
}

void UDialogueComponent::OnChoiceTimeout(FGuid SessionID, int32 DefaultChoiceIndex)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session || !Session->bWaiting) return;

    FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogWarning(
        FString::Printf(
            TEXT("Choice timeout in session %s — auto-selecting default choice %d."),
            *SessionID.ToString(), DefaultChoiceIndex));

    // Auto-submit through the same validated path as a player choice.
    // Chooser is passed as Sender so the Chooser check in Server_ReceiveChoice passes.
    Server_ReceiveChoice(SessionID, Session->GetChooser(), DefaultChoiceIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Called by UDialogueManagerComponent
// ─────────────────────────────────────────────────────────────────────────────

void UDialogueComponent::Server_ReceiveChoice(
    FGuid SessionID, AActor* Sender, int32 ChoiceIndex)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session) return;

    if (Session->GetChooser() != Sender)
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogWarning(
            FString::Printf(
                TEXT("Server_ReceiveChoice: non-Chooser actor '%s' submitted a choice — ignored."),
                *GetNameSafe(Sender)));
        return;
    }

    if (!Session->bWaiting) return;

    if (Session->TimeoutHandle.IsValid())
    {
        if (UWorld* World = GetWorld())
            World->GetTimerManager().ClearTimer(Session->TimeoutHandle);
    }

    const int32 TargetNode = FDialogueSimulator::ResolveChoice(*Session, ChoiceIndex);
    if (TargetNode == INDEX_NONE)
        return; // Locked or invalid choice — session remains waiting.

    Session->CurrentFrame().CurrentNodeIndex = TargetNode;
    Session->bWaiting = false;
    RunSession(*Session);
}

void UDialogueComponent::Server_ReceiveACK(FGuid SessionID, AActor* Sender)
{
    FDialogueSession* Session = ActiveSessions.Find(SessionID);
    if (!Session || !Session->bWaiting) return;

    // Validate sender is a participant in this session.
    const bool bIsParticipant = Session->Participants.ContainsByPredicate(
        [Sender](const TWeakObjectPtr<AActor>& P) { return P.Get() == Sender; });
    if (!bIsParticipant)
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogWarning(
            FString::Printf(
                TEXT("Server_ReceiveACK: non-participant actor '%s' — ignored."),
                *GetNameSafe(Sender)));
        return;
    }

    const UDialogueNode_Line* LineNode =
        Cast<UDialogueNode_Line>(
            Session->CurrentFrame().Asset
                ->GetNode(Session->CurrentFrame().CurrentNodeIndex));

    if (!LineNode)
    {
        FGameCoreBackend::GetLogging(GetLogDialogueTag()).LogWarning(
            TEXT("Server_ReceiveACK: current node is not a Line node."));
        return;
    }

    Session->CurrentFrame().CurrentNodeIndex = LineNode->NextNodeIndex;
    Session->bWaiting = false;
    RunSession(*Session);
}

void UDialogueComponent::Server_NotifyChooserDisconnect(FGuid SessionID)
{
    EndSession(SessionID, EDialogueEndReason::ChooserDisconnected);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

UDialogueManagerComponent* UDialogueComponent::GetManagerComponent(AActor* Actor)
{
    if (!Actor) return nullptr;

    // Try to find the manager on the actor directly first.
    if (UDialogueManagerComponent* Direct =
            Actor->FindComponentByClass<UDialogueManagerComponent>())
        return Direct;

    // Players: manager lives on APlayerState.
    if (const APawn* Pawn = Cast<APawn>(Actor))
    {
        if (APlayerState* PS = Pawn->GetPlayerState())
        {
            return PS->FindComponentByClass<UDialogueManagerComponent>();
        }
    }

    return nullptr;
}

FGuid UDialogueComponent::GetSessionIDForInstigator(const AActor* Instigator) const
{
    for (const auto& Pair : ActiveSessions)
    {
        const FDialogueSession& Session = Pair.Value;
        for (const TWeakObjectPtr<AActor>& P : Session.Participants)
        {
            if (P.Get() == Instigator)
                return Pair.Key;
        }
    }
    return FGuid();
}
