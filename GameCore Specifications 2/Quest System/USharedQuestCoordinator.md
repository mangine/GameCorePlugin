# USharedQuestCoordinator

**File:** `Quest/Components/SharedQuestCoordinator.h / .cpp`
**Type:** `UActorComponent` on the group/party actor
**Authority:** Server only

Owns the shared tracker truth for formally shared group quests. Individual `USharedQuestComponent` instances push increments through the coordinator. Uses `IGroupProvider` for group data reads. The enrollment handshake is delegated entirely to the external group system via `OnRequestGroupEnrollment`.

Lives on whatever actor your game designates as the group owner. That actor must implement `IGroupProvider`.

---

## Class Declaration

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class YOURGAME_API USharedQuestCoordinator : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Group Enrollment Delegate ─────────────────────────────────────────────
    //
    // Bind to integrate with your group/party system.
    // Called when a LeaderAccept quest needs a group enrollment handshake.
    // The bound function MUST eventually call OnEnrollmentResolved with the
    // confirmed member list — early (all accepted) or when grace expires.
    //
    // If unbound: coordinator auto-enrolls all InvitedMembers immediately.
    // Safe default for games without a party system.
    TDelegate<void(
        const FGameplayTag& /*QuestId*/,
        const TArray<APlayerState*>& /*InvitedMembers*/,
        float /*GraceSeconds*/,
        TFunction<void(const TArray<APlayerState*>&)> /*OnEnrollmentResolved*/
    )> OnRequestGroupEnrollment;

    // ── Enrollment ────────────────────────────────────────────────────────────

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

    // ── Tracker ────────────────────────────────────────────────────────────────

    // Route through here from USharedQuestComponent::Server_IncrementTracker.
    void IncrementSharedTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    int32 GetEnrolledMemberCount(const FGameplayTag& QuestId) const;

    // ── LeaderAccept ──────────────────────────────────────────────────────────

    // Called by USharedQuestComponent when the leader accepts a LeaderAccept quest.
    // Leader is already enrolled by the caller before this is invoked.
    void LeaderInitiateAccept(
        const FGameplayTag& QuestId,
        const USharedQuestDefinition* Def,
        const TArray<APlayerState*>& InvitedMembers);

private:
    struct FSharedQuestState
    {
        FGameplayTag QuestId;
        // Weak pointers — members may disconnect.
        TArray<TWeakObjectPtr<USharedQuestComponent>> Members;
        TMap<FGameplayTag, int32> SharedCounters;    // accumulated tracker values
        TMap<FGameplayTag, int32> EffectiveTargets;  // scaled for current group size
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

```cpp
void USharedQuestCoordinator::LeaderInitiateAccept(
    const FGameplayTag& QuestId,
    const USharedQuestDefinition* Def,
    const TArray<APlayerState*>& InvitedMembers)
{
    // Leader already enrolled by caller (USharedQuestComponent).
    if (!OnRequestGroupEnrollment.IsBound())
    {
        // No group system wired — auto-enroll all immediately.
        for (APlayerState* PS : InvitedMembers)
            if (USharedQuestComponent* QC =
                PS->FindComponentByClass<USharedQuestComponent>())
            {
                EnrollMember(QC, QuestId, EQuestMemberRole::Primary);
                QC->ServerRPC_AcceptQuest(QuestId);
            }
        return;
    }

    OnRequestGroupEnrollment.Execute(
        QuestId,
        InvitedMembers,
        Def->LeaderAcceptGraceSeconds,
        [this, QuestId](const TArray<APlayerState*>& ConfirmedMembers)
        {
            for (APlayerState* PS : ConfirmedMembers)
                if (USharedQuestComponent* QC =
                    PS->FindComponentByClass<USharedQuestComponent>())
                {
                    EnrollMember(QC, QuestId, EQuestMemberRole::Primary);
                    QC->ServerRPC_AcceptQuest(QuestId);
                }
        });
    // Group system now owns grace timer and opt-out tracking.
    // Empty ConfirmedMembers: leader remains sole enrolled member.
}
```

---

## Tracker Increment Flow

```cpp
void USharedQuestCoordinator::IncrementSharedTracker(
    const FGameplayTag& QuestId,
    const FGameplayTag& TrackerKey,
    int32 Delta)
{
    FSharedQuestState* State = ActiveSharedQuests.Find(QuestId);
    if (!State) return;

    int32& Shared   = State->SharedCounters.FindOrAdd(TrackerKey);
    const int32 Cap = State->EffectiveTargets.FindRef(TrackerKey);
    Shared = FMath::Min(Shared + Delta, Cap);

    DistributeTrackerUpdate(*State, TrackerKey, Shared);
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
```

---

## Member Leave / De-Scale Flow

```cpp
void USharedQuestCoordinator::RemoveMember(
    USharedQuestComponent* MemberQuestComp,
    const FGameplayTag& QuestId)
{
    FSharedQuestState* State = ActiveSharedQuests.Find(QuestId);
    if (!State) return;

    // Build a snapshot with de-scaled tracker values for the leaving member.
    FQuestRuntime Snapshot = BuildDeScaledSnapshot(*State, MemberQuestComp);
    MemberQuestComp->Server_ApplyGroupSnapshot(Snapshot);

    State->Members.RemoveAll([&](const TWeakObjectPtr<USharedQuestComponent>& M)
    {
        return !M.IsValid() || M.Get() == MemberQuestComp;
    });

    RecalculateEffectiveTargets(*State);

    // Distribute updated targets to remaining members.
    for (const FGameplayTag& Key : TArray<FGameplayTag>(State->SharedCounters.KeysArray()))
        DistributeTrackerUpdate(*State, Key, State->SharedCounters[Key]);

    FQuestMemberLeftPayload Payload;
    Payload.QuestId               = QuestId;
    Payload.LeavingMember         = MemberQuestComp->GetOwner<APlayerState>();
    Payload.RemainingMemberCount  = State->Members.Num();
    UGameCoreEventBus::Get(GetOwner()).Broadcast(
        TAG_GameCoreEvent_Quest_MemberLeft, Payload);
}
```

### De-Scale Formula

```
SoloTarget    = FQuestProgressTrackerDef::TargetValue  (from definition)
CurrentShared = SharedCounters[TrackerKey]
ScalingMult   = FQuestProgressTrackerDef::ScalingMultiplier

if ScalingMult <= 0.0:
    SnapshotValue = CurrentShared          // non-scalable: direct copy
else:
    SnapshotValue = Min(CurrentShared, Floor(CurrentShared / ScalingMult))
    SnapshotValue = Min(SnapshotValue, SoloTarget)  // hard cap at solo target
```

---

## `SingleAttempt` Member Independence

A `SingleAttempt` member failing does NOT fail the shared quest for the group.

```cpp
void USharedQuestCoordinator::OnMemberPersonalFail(
    USharedQuestComponent* MemberQuestComp,
    const FGameplayTag& QuestId)
{
    RemoveMember(MemberQuestComp, QuestId);

    FSharedQuestState* State = ActiveSharedQuests.Find(QuestId);
    if (!State) return;

    // Remove stale weak pointers.
    State->Members.RemoveAll([](const TWeakObjectPtr<USharedQuestComponent>& M)
    {
        return !M.IsValid();
    });

    if (State->Members.IsEmpty())
    {
        // All members failed or left — fail the shared quest.
        // The last remaining member's UQuestComponent will have already
        // called Internal_FailQuest via its own tracker path.
        ActiveSharedQuests.Remove(QuestId);
    }
}
```
