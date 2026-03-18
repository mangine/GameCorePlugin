# USharedQuestCoordinator

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**File:** `Quest/Components/SharedQuestCoordinator.h / .cpp` 
**Type:** `UActorComponent` on the group/party actor 
**Authority:** Server only

Owns the shared tracker truth for formally shared group quests. Individual `USharedQuestComponent` instances push tracker updates through the coordinator. Uses `IGroupProvider` for data reads. The enrollment handshake (invite/accept/opt-out) is delegated entirely to the external group system via `OnRequestGroupEnrollment`.

Lives on whatever actor your game designates as the group owner. That actor must implement `IGroupProvider`. The quest system does not care whether it is a party actor, a ship actor, or a squad manager.

---

## Class Declaration

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API USharedQuestCoordinator : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Group Enrollment Delegate ────────────────────────────────────────────

    // Bind this delegate to integrate with your group/party system.
    //
    // Called by the coordinator when a LeaderAccept quest needs a group
    // enrollment handshake. The bound function must eventually call
    // OnEnrollmentResolved with the final confirmed member list — either
    // early (all members accepted) or when the grace window expires.
    //
    // Parameters:
    //   QuestId           — the quest being accepted
    //   InvitedMembers    — members to invite (leader excluded, already enrolled)
    //   GraceSeconds      — maximum window before auto-resolving
    //   OnEnrollmentResolved — callback the group system calls with confirmed members
    //
    // If unbound: coordinator auto-enrolls all InvitedMembers immediately
    // (no grace window, no opt-out). Safe default for games without a party system.
    TDelegate<void(
        const FGameplayTag& /*QuestId*/,
        const TArray<APlayerState*>& /*InvitedMembers*/,
        float /*GraceSeconds*/,
        TFunction<void(const TArray<APlayerState*>& /*ConfirmedMembers*/)> /*OnEnrollmentResolved*/
    )> OnRequestGroupEnrollment;

    // ── Enrollment ──────────────────────────────────────────────────────────

    void EnrollMember(
        USharedQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId,
        EQuestMemberRole Role = EQuestMemberRole::Primary);

    // Called when a member leaves the group or disconnects.
    // Builds a de-scaled snapshot and applies it to the leaving member's component.
    // Recalculates EffectiveTargets for remaining members.
    void RemoveMember(
        USharedQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId);

    // ── Tracker ─────────────────────────────────────────────────────────────────

    void IncrementSharedTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    int32 GetEnrolledMemberCount(const FGameplayTag& QuestId) const;

    // ── LeaderAccept flow ───────────────────────────────────────────────────

    // Called by USharedQuestComponent when the leader accepts a LeaderAccept quest.
    // InvitedMembers is the current group member list minus the leader.
    // Leader is enrolled immediately by the caller before this is invoked.
    void LeaderInitiateAccept(
        const FGameplayTag& QuestId,
        const USharedQuestDefinition* Def,
        const TArray<APlayerState*>& InvitedMembers);

private:
    struct FSharedQuestState
    {
        FGameplayTag QuestId;
        TArray<TWeakObjectPtr<USharedQuestComponent>> Members;
        TMap<FGameplayTag, int32> SharedCounters;
        TMap<FGameplayTag, int32> EffectiveTargets;
        TObjectPtr<const USharedQuestDefinition> Definition;
    };

    TMap<FGameplayTag, FSharedQuestState> ActiveSharedQuests;

    void RecalculateEffectiveTargets(FSharedQuestState& State);
    void DistributeTrackerUpdate(
        FSharedQuestState& State,
        const FGameplayTag& TrackerKey,
        int32 NewSharedValue);
    FQuestRuntime BuildDeScaledSnapshot(
        const FSharedQuestState& State,
        USharedQuestComponent* LeavingMember) const;
    void OnMemberPersonalFail(
        USharedQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId);
};
```

---

## LeaderAccept Flow

The coordinator owns the shared quest state and tracker truth. The external group system owns the invite/accept/opt-out handshake. They connect via `OnRequestGroupEnrollment`.

```
LeaderInitiateAccept(QuestId, Def, InvitedMembers)
  1. Create FSharedQuestState for QuestId
     (leader already enrolled by USharedQuestComponent before this call)
  2. If OnRequestGroupEnrollment.IsBound():
       Call OnRequestGroupEnrollment(
           QuestId,
           InvitedMembers,
           Def->LeaderAcceptGraceSeconds,
           [this, QuestId](const TArray<APlayerState*>& ConfirmedMembers)
           {
               // Called by group system when ready — early or on timeout.
               for (APlayerState* PS : ConfirmedMembers)
               {
                   if (USharedQuestComponent* QC =
                       PS->FindComponentByClass<USharedQuestComponent>())
                   {
                       EnrollMember(QC, QuestId, EQuestMemberRole::Primary);
                       QC->ServerRPC_AcceptQuest(QuestId); // triggers base accept
                   }
               }
           }
       )
     // Group system now owns the grace timer, opt-out tracking, and early resolve.
     // Coordinator waits for the callback — no timer managed here.

  3. If OnRequestGroupEnrollment is NOT bound (no group system wired):
       // Auto-enroll all invited members immediately.
       for (APlayerState* PS : InvitedMembers)
       {
           if (USharedQuestComponent* QC =
               PS->FindComponentByClass<USharedQuestComponent>())
           {
               EnrollMember(QC, QuestId, EQuestMemberRole::Primary);
               QC->ServerRPC_AcceptQuest(QuestId);
           }
       }
```

> **Early accept:** the group system calls `OnEnrollmentResolved` as soon as all members confirm, before `GraceSeconds` expires. The coordinator receives the callback and enrolls immediately — no timer awareness needed on the quest system side.

> **All declined / timeout with zero confirms:** group system calls `OnEnrollmentResolved` with an empty array. Coordinator receives it and no additional members are enrolled. The leader remains the sole enrolled member and continues solo.

---

## Tracker Increment Flow

```
IncrementSharedTracker(QuestId, TrackerKey, Delta)
  1. SharedCounters[TrackerKey] = Min(SharedCounters[TrackerKey] + Delta,
                                      EffectiveTargets[TrackerKey])
  2. DistributeTrackerUpdate:
       For each valid Member:
         int32 MemberCurrent = Member->GetTrackerValue(QuestId, TrackerKey)
         int32 Diff = SharedCounters[TrackerKey] - MemberCurrent
         if Diff > 0:
           Member->Server_IncrementTracker(QuestId, TrackerKey, Diff)
         // Diff converges member to shared value without a separate setter.
  3. Broadcast GMS: GameCoreEvent.Quest.TrackerUpdated
```

---

## Member Leave / De-Scale Flow

```
RemoveMember(MemberQuestComp, QuestId)
  1. BuildDeScaledSnapshot:
       For each tracker T in SharedCounters:
         SoloTarget  = Def->FindStage(CurrentStage)->FindTracker(T).TargetValue
         ScalingMult = Def->FindStage(CurrentStage)->FindTracker(T).ScalingMultiplier
         if ScalingMult <= 0.0:
           SnapshotValue = SharedCounters[T]          // non-scalable: direct copy
         else:
           SnapshotValue = Min(SharedCounters[T],
               Floor(SharedCounters[T] / ScalingMult))
           SnapshotValue = Min(SnapshotValue, SoloTarget)  // hard cap at solo target
  2. MemberQuestComp->Server_ApplyGroupSnapshot(SnapshotRuntime)
  3. Remove member from State.Members
  4. RecalculateEffectiveTargets (remaining group size changed)
  5. DistributeTrackerUpdate for all trackers (remaining members adjust)
```

### De-Scale Formula

```
SoloTarget    = FQuestProgressTrackerDef::TargetValue
CurrentShared = SharedCounters[TrackerKey]
ScalingMult   = FQuestProgressTrackerDef::ScalingMultiplier

if ScalingMult <= 0.0:
    SnapshotValue = CurrentShared
else:
    SnapshotValue = Min(CurrentShared, Floor(CurrentShared / ScalingMult))
    SnapshotValue = Min(SnapshotValue, SoloTarget)
```

---

## SingleAttempt Member Independence

A `SingleAttempt` member failing does not fail the shared quest for the group.

```cpp
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
    {
        // All members failed or left — fail the shared quest entirely.
        // Broadcast GMS: GameCoreEvent.Quest.Failed
        ActiveSharedQuests.Remove(QuestId);
    }
}
```

---

## Wiring Example

```cpp
// In your party system's BeginPlay or initialization:
USharedQuestCoordinator* Coordinator =
    PartyActor->FindComponentByClass<USharedQuestCoordinator>();

Coordinator->OnRequestGroupEnrollment.BindUObject(
    this, &UMyPartyComponent::HandleQuestEnrollmentRequest);

// In UMyPartyComponent:
void UMyPartyComponent::HandleQuestEnrollmentRequest(
    const FGameplayTag& QuestId,
    const TArray<APlayerState*>& InvitedMembers,
    float GraceSeconds,
    TFunction<void(const TArray<APlayerState*>&)> OnEnrollmentResolved)
{
    // Send UI invites to each member.
    // Track who accepts / declines.
    // Start grace timer.
    // On all-accept OR timer expiry: call OnEnrollmentResolved(ConfirmedList).
    // On early all-accept: call OnEnrollmentResolved(ConfirmedList) immediately.
    MyPartyInviteFlow.Start(
        QuestId, InvitedMembers, GraceSeconds, MoveTemp(OnEnrollmentResolved));
}
```
