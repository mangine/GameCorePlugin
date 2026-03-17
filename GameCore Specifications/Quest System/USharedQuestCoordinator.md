# USharedQuestCoordinator

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**File:** `Quest/Components/SharedQuestCoordinator.h / .cpp` 
**Type:** `UActorComponent` on the group/party actor 
**Authority:** Server only

Owns the shared tracker truth for formally shared group quests. Individual `USharedQuestComponent` instances push/pull tracker updates through the coordinator. Uses `IGroupProvider` exclusively — no concrete party system dependency.

The coordinator lives on whatever actor your game designates as the group owner. That actor must implement `IGroupProvider`. If your game uses an `APartyActor`, it goes there. If it uses a ship actor, it goes there. The quest system does not care.

---

## Class Declaration

```cpp
UCLASS(ClassGroup=(PirateGame), meta=(BlueprintSpawnableComponent))
class PIRATEQUESTS_API USharedQuestCoordinator : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Enrollment ──────────────────────────────────────────────────────────

    void EnrollMember(
        USharedQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId,
        EQuestMemberRole Role = EQuestMemberRole::Primary);

    void RemoveMember(
        USharedQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId);

    // ── Tracker ─────────────────────────────────────────────────────────────────

    // Increments the shared counter and distributes delta to all enrolled members.
    void IncrementSharedTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    int32 GetGroupSize(const FGameplayTag& QuestId) const;

    // ── LeaderAccept flow ───────────────────────────────────────────────────

    void LeaderInitiateAccept(
        const FGameplayTag& QuestId,
        const USharedQuestDefinition* Def,
        IGroupProvider* Provider);

    void OptOut(USharedQuestComponent* MemberQuestComp, const FGameplayTag& QuestId);

private:
    struct FSharedQuestState
    {
        FGameplayTag QuestId;
        TArray<TWeakObjectPtr<USharedQuestComponent>> Members;
        TMap<FGameplayTag, int32>  SharedCounters;
        TMap<FGameplayTag, int32>  EffectiveTargets;
        TObjectPtr<const USharedQuestDefinition> Definition;
        FTimerHandle GraceTimer;
        TArray<TWeakObjectPtr<USharedQuestComponent>> PendingOptOuts;
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
         // Diff sends the exact delta needed to converge member to shared value.
  3. Broadcast GMS: GameCoreEvent.Quest.TrackerUpdated
```

> Distributing as a diff (not absolute value) keeps `Server_IncrementTracker` the single write path — no separate setter needed.

---

## Member Leave / De-Scale Flow

```
RemoveMember(MemberQuestComp, QuestId)
  1. BuildDeScaledSnapshot:
       For each tracker T:
         SoloTarget = Def->FindStage(CurrentStage)->FindTracker(T).TargetValue
         ScalingMult = Def->FindStage(CurrentStage)->FindTracker(T).ScalingMultiplier
         if ScalingMult <= 0.0:
           SnapshotValue = SharedCounters[T]        // non-scalable: direct copy
         else:
           SnapshotValue = Min(SharedCounters[T],
               Floor(SharedCounters[T] / ScalingMult))
           SnapshotValue = Min(SnapshotValue, SoloTarget)  // hard cap
  2. MemberQuestComp->Server_ApplyGroupSnapshot(SnapshotRuntime)
  3. Remove member from State.Members
  4. RecalculateEffectiveTargets (party size changed)
  5. DistributeTrackerUpdate for all trackers (remaining members' EffectiveTargets update)
```

### De-Scale Formula

```
SoloTarget      = FQuestProgressTrackerDef::TargetValue
CurrentShared   = SharedCounters[TrackerKey]
ScalingMult     = FQuestProgressTrackerDef::ScalingMultiplier

if ScalingMult <= 0.0:
    SnapshotValue = CurrentShared           // non-scalable
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

    // Prune dead weak pointers
    State->Members.RemoveAll([](const TWeakObjectPtr<USharedQuestComponent>& M)
    {
        return !M.IsValid();
    });

    if (State->Members.IsEmpty())
    {
        // All members failed or left — fail the shared quest
        // Broadcast GMS: GameCoreEvent.Quest.Failed (shared)
        ActiveSharedQuests.Remove(QuestId);
    }
}
```

---

## LeaderAccept Grace Period

```
LeaderInitiateAccept(QuestId, Def, Provider)
  1. Create FSharedQuestState, enroll leader immediately (Primary)
  2. Notify all other group members via GMS: GameCoreEvent.Quest.PartyInvite
     (quest system fires event — UI/party system delivers the notification)
  3. Start GraceTimer(Def->LeaderAcceptGraceSeconds):
       On expire:
         For each member NOT in PendingOptOuts AND still in group:
           EnrollMember(member, QuestId)
         PendingOptOuts members are excluded — not enrolled
```
