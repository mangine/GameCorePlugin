#include "Quest/Components/SharedQuestCoordinator.h"
#include "Quest/Components/SharedQuestComponent.h"
#include "Quest/Data/SharedQuestDefinition.h"
#include "Quest/Data/QuestStageDefinition.h"
#include "Quest/Events/QuestEventPayloads.h"
#include "EventBus/GameCoreEventBus.h"
#include "GameFramework/PlayerState.h"

// ---------------------------------------------------------------------------
// Enrollment
// ---------------------------------------------------------------------------

void USharedQuestCoordinator::EnrollMember(
    USharedQuestComponent* MemberQuestComp,
    const FGameplayTag& QuestId,
    EQuestMemberRole Role)
{
    if (!MemberQuestComp) return;

    FSharedQuestState& State = ActiveSharedQuests.FindOrAdd(QuestId);
    State.QuestId = QuestId;
    State.Members.AddUnique(MemberQuestComp);

    RecalculateEffectiveTargets(State);
}

void USharedQuestCoordinator::RemoveMember(
    USharedQuestComponent* MemberQuestComp,
    const FGameplayTag& QuestId)
{
    FSharedQuestState* State = ActiveSharedQuests.Find(QuestId);
    if (!State) return;

    FQuestRuntime Snapshot = BuildDeScaledSnapshot(*State, MemberQuestComp);
    MemberQuestComp->Server_ApplyGroupSnapshot(Snapshot);

    State->Members.RemoveAll([&](const TWeakObjectPtr<USharedQuestComponent>& M)
    {
        return !M.IsValid() || M.Get() == MemberQuestComp;
    });

    RecalculateEffectiveTargets(*State);

    // Redistribute current shared values to remaining members with updated targets.
    for (const auto& CounterPair : State->SharedCounters)
        DistributeTrackerUpdate(*State, CounterPair.Key, CounterPair.Value);

    FQuestMemberLeftPayload Payload;
    Payload.QuestId              = QuestId;
    Payload.LeavingMember        = MemberQuestComp->GetOwner<APlayerState>();
    Payload.RemainingMemberCount = State->Members.Num();
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(GetOwner()))
        Bus->Broadcast(TAG_GameCoreEvent_Quest_MemberLeft, Payload);
}

int32 USharedQuestCoordinator::GetEnrolledMemberCount(const FGameplayTag& QuestId) const
{
    const FSharedQuestState* State = ActiveSharedQuests.Find(QuestId);
    return State ? State->Members.Num() : 0;
}

// ---------------------------------------------------------------------------
// LeaderAccept
// ---------------------------------------------------------------------------

void USharedQuestCoordinator::LeaderInitiateAccept(
    const FGameplayTag& QuestId,
    const USharedQuestDefinition* Def,
    const TArray<APlayerState*>& InvitedMembers)
{
    if (!OnRequestGroupEnrollment.IsBound())
    {
        // Auto-enroll all members immediately.
        for (APlayerState* PS : InvitedMembers)
        {
            if (USharedQuestComponent* QC =
                PS->FindComponentByClass<USharedQuestComponent>())
            {
                EnrollMember(QC, QuestId, EQuestMemberRole::Primary);
                QC->ServerRPC_AcceptQuest(QuestId);
            }
        }
        return;
    }

    float GraceSeconds = Def ? Def->LeaderAcceptGraceSeconds : 10.0f;
    OnRequestGroupEnrollment.Execute(
        QuestId,
        InvitedMembers,
        GraceSeconds,
        [this, QuestId](const TArray<APlayerState*>& ConfirmedMembers)
        {
            for (APlayerState* PS : ConfirmedMembers)
            {
                if (USharedQuestComponent* QC =
                    PS->FindComponentByClass<USharedQuestComponent>())
                {
                    EnrollMember(QC, QuestId, EQuestMemberRole::Primary);
                    QC->ServerRPC_AcceptQuest(QuestId);
                }
            }
        });
}

// ---------------------------------------------------------------------------
// Tracker
// ---------------------------------------------------------------------------

void USharedQuestCoordinator::IncrementSharedTracker(
    const FGameplayTag& QuestId,
    const FGameplayTag& TrackerKey,
    int32 Delta)
{
    FSharedQuestState* State = ActiveSharedQuests.Find(QuestId);
    if (!State) return;

    int32& Shared   = State->SharedCounters.FindOrAdd(TrackerKey);
    const int32 Cap = State->EffectiveTargets.FindRef(TrackerKey);
    Shared = FMath::Min(Shared + Delta, Cap > 0 ? Cap : INT32_MAX);

    DistributeTrackerUpdate(*State, TrackerKey, Shared);
}

// ---------------------------------------------------------------------------
// Internal
// ---------------------------------------------------------------------------

void USharedQuestCoordinator::RecalculateEffectiveTargets(FSharedQuestState& State)
{
    if (!State.Definition) return;

    const int32 GroupSize = State.Members.Num();

    // Iterate stage definitions to find tracker defs for the current stage.
    // Use the first enrolled member's current stage as the reference.
    for (const TWeakObjectPtr<USharedQuestComponent>& WeakMember : State.Members)
    {
        USharedQuestComponent* Member = WeakMember.Get();
        if (!Member) continue;

        const FQuestRuntime* Runtime = Member->FindActiveQuest(State.QuestId);
        if (!Runtime) continue;

        const UQuestStageDefinition* Stage =
            State.Definition->FindStage(Runtime->CurrentStageTag);
        if (!Stage) break;

        for (const FQuestProgressTrackerDef& TrackerDef : Stage->Trackers)
        {
            if (!TrackerDef.bReEvaluateOnly)
                State.EffectiveTargets.Add(
                    TrackerDef.TrackerKey,
                    TrackerDef.GetEffectiveTarget(GroupSize));
        }
        break;
    }
}

void USharedQuestCoordinator::DistributeTrackerUpdate(
    FSharedQuestState& State,
    const FGameplayTag& TrackerKey,
    int32 NewSharedValue)
{
    for (const TWeakObjectPtr<USharedQuestComponent>& WeakMember : State.Members)
    {
        USharedQuestComponent* Member = WeakMember.Get();
        if (!Member) continue;

        const FQuestRuntime* Runtime = Member->FindActiveQuest(State.QuestId);
        if (!Runtime) continue;

        const FQuestTrackerEntry* Entry = Runtime->FindTracker(TrackerKey);
        const int32 MemberCurrent = Entry ? Entry->CurrentValue : 0;
        const int32 Diff = NewSharedValue - MemberCurrent;
        if (Diff > 0)
            Member->Server_IncrementTracker(State.QuestId, TrackerKey, Diff);
    }
}

FQuestRuntime USharedQuestCoordinator::BuildDeScaledSnapshot(
    const FSharedQuestState& State,
    USharedQuestComponent* LeavingMember) const
{
    FQuestRuntime Snapshot;
    Snapshot.QuestId = State.QuestId;

    const FQuestRuntime* MemberRuntime = LeavingMember->FindActiveQuest(State.QuestId);
    if (MemberRuntime)
    {
        Snapshot.CurrentStageTag        = MemberRuntime->CurrentStageTag;
        Snapshot.LastCompletedTimestamp = MemberRuntime->LastCompletedTimestamp;
    }

    if (!State.Definition) return Snapshot;

    // De-scale formula:
    // ScalingMult <= 0: SnapshotValue = CurrentShared (non-scalable, direct copy)
    // ScalingMult >  0: SnapshotValue = Min(CurrentShared, Floor(CurrentShared / ScalingMult))
    //                   capped at SoloTarget
    for (const auto& CounterPair : State.SharedCounters)
    {
        const FGameplayTag& TrackerKey  = CounterPair.Key;
        const int32         SharedValue = CounterPair.Value;

        // Find tracker definition for scaling parameters.
        const FQuestStageDefinition* Stage =
            MemberRuntime
                ? State.Definition->FindStage(MemberRuntime->CurrentStageTag)
                : nullptr;

        const FQuestProgressTrackerDef* TrackerDef =
            Stage ? Stage->FindTrackerDef(TrackerKey) : nullptr;

        int32 SnapshotValue = SharedValue;
        int32 SoloTarget    = TrackerDef ? TrackerDef->TargetValue : SharedValue;

        if (TrackerDef && TrackerDef->ScalingMultiplier > 0.0f)
        {
            SnapshotValue = FMath::Min(
                SharedValue,
                FMath::FloorToInt(SharedValue / TrackerDef->ScalingMultiplier));
            SnapshotValue = FMath::Min(SnapshotValue, SoloTarget);
        }

        FQuestTrackerEntry SnapshotEntry;
        SnapshotEntry.TrackerKey      = TrackerKey;
        SnapshotEntry.CurrentValue    = SnapshotValue;
        SnapshotEntry.EffectiveTarget = SoloTarget;
        Snapshot.Trackers.Add(SnapshotEntry);
    }

    return Snapshot;
}

void USharedQuestCoordinator::OnMemberPersonalFail(
    USharedQuestComponent* MemberQuestComp,
    const FGameplayTag& QuestId)
{
    RemoveMember(MemberQuestComp, QuestId);

    FSharedQuestState* State = ActiveSharedQuests.Find(QuestId);
    if (!State) return;

    State->Members.RemoveAll([](const TWeakObjectPtr<USharedQuestComponent>& M)
    {
        return !M.IsValid();
    });

    if (State->Members.IsEmpty())
        ActiveSharedQuests.Remove(QuestId);
}
