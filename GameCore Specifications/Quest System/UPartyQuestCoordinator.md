# UPartyQuestCoordinator

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**File:** `Quest/Components/PartyQuestCoordinator.h / .cpp` 
**Type:** `UActorComponent` on the Party actor 
**Authority:** Server only (no client replication — member `UQuestComponent` data is what replicates to clients)

Owns the shared tracker truth for party quests. Individual `UQuestComponent` instances subscribe to receive tracker updates. Does not replace `UQuestComponent` — every player still has their own quest runtime; the coordinator is the write authority for tracker increments.

---

## Class Declaration

```cpp
UCLASS(ClassGroup=(PirateGame), meta=(BlueprintSpawnableComponent))
class PIRATEQUESTS_API UPartyQuestCoordinator : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Party Quest Enrollment ───────────────────────────────────────────────

    // Called when a player accepts a party quest.
    // Registers them as a member and starts or joins the shared tracker.
    void EnrollMember(
        UQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId,
        EQuestMemberRole Role = EQuestMemberRole::Primary);

    // Called when a player leaves the party or disconnects.
    // Performs de-scaled snapshot and removes them from the coordinator.
    void RemoveMember(
        UQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId);

    // Increment a tracker for a quest. Distributes to all enrolled members.
    void IncrementSharedTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    // Returns current party size for a given quest (enrolled members only).
    int32 GetPartySize(const FGameplayTag& QuestId) const;

    // ── LeaderAccept flow ───────────────────────────────────────────────────

    // Leader triggers enrollment for all present party members.
    // Starts LeaderAcceptGraceSeconds timer. Members can opt out during this window.
    void LeaderInitiateQuestAccept(
        const FGameplayTag& QuestId,
        const UQuestDefinition* Def);

    // Member explicitly opts out during the grace window.
    void OptOutOfQuestAccept(
        UQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId);

private:
    struct FPartyQuestState
    {
        FGameplayTag QuestId;

        // All enrolled members and their components.
        // Weak pointers — component may be destroyed on disconnect.
        TArray<TWeakObjectPtr<UQuestComponent>> Members;

        // Shared tracker values. These are the authoritative counters.
        // UQuestComponent tracker entries mirror these values.
        TMap<FGameplayTag, int32> SharedCounters;

        // Effective targets per tracker at current party size.
        TMap<FGameplayTag, int32> EffectiveTargets;

        // Cached definition. Soft reference resolved at enrollment.
        TObjectPtr<const UQuestDefinition> Definition;

        // Grace period timer handle (LeaderAccept only).
        FTimerHandle GraceTimer;
        TArray<TWeakObjectPtr<UQuestComponent>> PendingOptOuts;
    };

    // Keyed by QuestId
    TMap<FGameplayTag, FPartyQuestState> ActivePartyQuests;

    void RecalculateEffectiveTargets(
        FPartyQuestState& State);

    void DistributeTrackerUpdate(
        FPartyQuestState& State,
        const FGameplayTag& TrackerKey,
        int32 NewValue);

    FQuestRuntime BuildDeScaledSnapshot(
        const FPartyQuestState& State,
        UQuestComponent* LeavingMember) const;
};
```

---

## Tracker Increment Flow

```
IncrementSharedTracker(QuestId, TrackerKey, Delta)
  1. Find FPartyQuestState for QuestId
  2. SharedCounters[TrackerKey] += Delta
  3. Clamp to EffectiveTargets[TrackerKey]
  4. DistributeTrackerUpdate:
       For each Member in State.Members (alive weak ptr):
         Member->Server_IncrementTracker(QuestId, TrackerKey,
             SharedCounters[TrackerKey] - Member->GetTrackerValue(QuestId, TrackerKey))
         // Delta is the difference so member mirrors the shared value exactly.
  5. Broadcast GMS: GameCoreEvent.Quest.TrackerUpdated
```

> **Why distribute as diff, not absolute:** `UQuestComponent::Server_IncrementTracker` expects a delta. Sending the difference between shared and member value ensures the member's counter converges to the shared value without needing a separate setter RPC.

---

## Member Leave / Disconnect Flow

```
RemoveMember(MemberQuestComp, QuestId)
  1. Find FPartyQuestState for QuestId
  2. BuildDeScaledSnapshot:
       For each tracker T in State.SharedCounters:
         SnapshotValue = Min(SharedCounters[T],
             Floor(SharedCounters[T] / ScalingMultiplier))
         // For non-scalable trackers (ScalingMultiplier == 0 or 1.0):
         //   SnapshotValue = SharedCounters[T] (direct copy)
         // This prevents trackers exceeding the solo target.
       Build FQuestRuntime with SnapshotValue entries and cap EffectiveTarget
       to solo TargetValue.
  3. MemberQuestComp->Server_ApplyPartySnapshot(SnapshotRuntime)
  4. Remove member from State.Members
  5. RecalculateEffectiveTargets (remaining party size shrunk)
  6. DistributeTrackerUpdate for all trackers with new EffectiveTargets
     (other members' progress bars adjust)
```

### De-Scale Formula

```
SoloTarget = TrackerDef.TargetValue  (base, no scaling)
CurrentShared = SharedCounters[TrackerKey]
ScalingMultiplier = TrackerDef.ScalingMultiplier

if ScalingMultiplier <= 0.0:
    SnapshotValue = CurrentShared  // non-scalable, direct copy
else:
    SnapshotValue = Min(CurrentShared, Floor(CurrentShared / ScalingMultiplier))
    SnapshotValue = Min(SnapshotValue, SoloTarget)  // hard cap at solo target
```

---

## LeaderAccept Grace Period

```
LeaderInitiateQuestAccept(QuestId, Def)
  1. Create FPartyQuestState for QuestId
  2. Enroll leader immediately (Primary role)
  3. For each other party member:
       Send notification: "QuestId available — auto-accepting in N seconds"
       (via GMS or party RPC — quest system fires event, party system delivers)
  4. Start GraceTimer(Def->LeaderAcceptGraceSeconds):
       On expire:
         For each member NOT in PendingOptOuts:
           EnrollMember(member, QuestId)
         PendingOptOuts are excluded; they are NOT enrolled.

OptOutOfQuestAccept(MemberQuestComp, QuestId)
  1. Find FPartyQuestState
  2. Add MemberQuestComp to PendingOptOuts
  3. (Timer continues for remaining members)
```

---

## SingleAttempt Member Independence

When a party includes members with different lifecycle states for the same quest:

- Each member's `EQuestLifecycle` is their own — stored on `UQuestDefinition`, same for all, but `MemberRole` in `FQuestRuntime` governs their personal outcome.
- If a `SingleAttempt` member fails (e.g. dies in a dungeon and the quest has a death-fail rule): that member's `UQuestComponent` marks the quest as permanently closed **for them only**. The coordinator does not fail the shared quest.
- The failed member is removed from `State.Members` via `RemoveMember`. Remaining members continue.
- The coordinator only fails the shared quest if **all** members have failed or left.

```cpp
// Called by UQuestComponent when a member's personal fail condition is met
void UPartyQuestCoordinator::OnMemberPersonalFail(
    UQuestComponent* MemberQuestComp,
    const FGameplayTag& QuestId)
{
    RemoveMember(MemberQuestComp, QuestId);

    FPartyQuestState* State = ActivePartyQuests.Find(QuestId);
    if (!State) return;

    // Clean up dead weak pointers
    State->Members.RemoveAll([](const TWeakObjectPtr<UQuestComponent>& M)
    {
        return !M.IsValid();
    });

    if (State->Members.IsEmpty())
    {
        // All members gone — fail the shared quest
        // Broadcast GMS: GameCoreEvent.Quest.Failed (party quest)
        ActivePartyQuests.Remove(QuestId);
    }
}
```
