# URaidComponent

**File:** `Group/RaidComponent.h` / `RaidComponent.cpp`
**Authority:** Server only
**Owner:** `ARaidActor`

Holds the group list, raid leader, and raid assistant authority model. Responsible for recomputing raid roles, dissolving the raid when only one group remains, and providing cross-group `FGroupMemberEntry` data for `BuildViewForMember`.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API URaidComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Group management ──────────────────────────────────────────────────

    // Adds a group to the raid. The group leader becomes GroupLeader raid role.
    // Returns false if raid is at MaxGroupsPerRaid.
    bool AddGroup(AGroupActor* Group);

    // Removes a group. Transfers raid leadership if needed.
    // Dissolves raid (via UGroupSubsystem) if only one group remains.
    void RemoveGroup(AGroupActor* Group);

    // ── Raid leadership ───────────────────────────────────────────────────

    APlayerState* GetRaidLeader() const { return RaidLeaderPS.Get(); }

    // Transfers raid leadership to NewLeader who must be a group leader in the raid.
    bool TransferRaidLeadership(APlayerState* CallerPS, APlayerState* NewLeader);

    // Promotes TargetPS to raid assistant. Only the raid leader can call this.
    bool PromoteToAssistant(APlayerState* CallerPS, APlayerState* TargetPS);

    // Demotes TargetPS from raid assistant. Only the raid leader can call this.
    bool DemoteAssistant(APlayerState* CallerPS, APlayerState* TargetPS);

    // ── Kick ─────────────────────────────────────────────────────────────

    // Raid leader or any assistant can kick any member across all groups.
    // Assistants cannot kick the raid leader or other assistants.
    // Returns false if CallerPS has insufficient authority.
    bool KickMember(APlayerState* CallerPS, APlayerState* TargetPS);

    // ── Queries ───────────────────────────────────────────────────────────

    int32 GetGroupCount() const { return Groups.Num(); }
    int32 GetGroupIndex(const AGroupActor* Group) const;
    bool  IsRaidLeader(const APlayerState* PS) const    { return RaidLeaderPS.Get() == PS; }
    bool  IsRaidAssistant(const APlayerState* PS) const;
    void  GetAllRaidMembers(TArray<APlayerState*>& Out) const;
    AGroupActor* FindGroupForPlayer(const APlayerState* PS) const;

    // Called by UGroupComponent::BuildViewForMember to get cross-group entries.
    // ForGroup's own members are excluded. GroupSharedData is intentionally null on all entries.
    void PopulateRaidMemberEntries(
        const AGroupActor* ForGroup,
        TArray<FGroupMemberEntry>& OutRaidMembers) const;

private:
    UPROPERTY()
    TArray<TObjectPtr<AGroupActor>> Groups; // ordered by join time

    TWeakObjectPtr<APlayerState> RaidLeaderPS;
    TArray<TWeakObjectPtr<APlayerState>> Assistants;

    const UGroupConfigDataAsset* GetConfig() const;

    // Recomputes EGroupRaidRole for every member across all groups.
    void AssignRaidRoles();

    // Pushes updated views to all groups by calling PushViewToAllMembers on each.
    void BroadcastRaidViewToAllGroups();

    // Transfers raid leadership to the first valid group leader found.
    void TransferRaidLeadershipToNextGroup();
};
```

---

## AddGroup

```cpp
bool URaidComponent::AddGroup(AGroupActor* Group)
{
    if (!Group) return false;
    if (Groups.Num() >= GetConfig()->MaxGroupsPerRaid) return false;
    if (Groups.Contains(Group)) return false;

    Groups.Add(Group);
    Group->OwningRaid = GetOwner<ARaidActor>(); // friend access

    AssignRaidRoles();
    BroadcastRaidViewToAllGroups();

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::Raid_GroupJoined,
        FInstancedStruct::Make(FRaidGroupChangedMessage{ GetOwner<ARaidActor>(), Group }),
        EGameCoreEventScope::ServerOnly);
    return true;
}
```

---

## RemoveGroup

```cpp
void URaidComponent::RemoveGroup(AGroupActor* Group)
{
    if (!Groups.Remove(Group)) return;

    Group->OwningRaid = nullptr;

    // If the raid leader belonged to the leaving group, transfer leadership.
    if (APlayerState* Leader = RaidLeaderPS.Get())
    {
        AGroupActor* LeaderGroup = FindGroupForPlayer(Leader);
        if (!LeaderGroup)
            TransferRaidLeadershipToNextGroup();
    }

    // Remove assistants that belonged to the leaving group.
    Assistants.RemoveAll([Group](const TWeakObjectPtr<APlayerState>& W)
    {
        APlayerState* PS = W.Get();
        return !PS || Group->GetGroupComponent()->IsMember(PS);
    });

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::Raid_GroupLeft,
        FInstancedStruct::Make(FRaidGroupChangedMessage{ GetOwner<ARaidActor>(), Group }),
        EGameCoreEventScope::ServerOnly);

    if (Groups.Num() <= 1)
    {
        // Dissolve raid — only one group left.
        if (Groups.Num() == 1)
            Groups[0]->OwningRaid = nullptr;

        UGameCoreEventBus::Get(this)->Broadcast(
            GameCoreGroupTags::Raid_Disbanded,
            FInstancedStruct::Make(FRaidDisbandedMessage{ GetOwner<ARaidActor>() }),
            EGameCoreEventScope::ServerOnly);

        Groups.Empty();
        GetWorld()->GetSubsystem<UGroupSubsystem>()->RemoveGroupFromRaid(
            nullptr, GetOwner<ARaidActor>());
        return;
    }

    AssignRaidRoles();
    BroadcastRaidViewToAllGroups();
}
```

---

## KickMember

```cpp
bool URaidComponent::KickMember(APlayerState* CallerPS, APlayerState* TargetPS)
{
    if (!CallerPS || !TargetPS) return false;

    bool bHasAuthority = IsRaidLeader(CallerPS) || IsRaidAssistant(CallerPS);
    if (!bHasAuthority) return false;

    // Assistants cannot kick the raid leader or other assistants.
    if (IsRaidAssistant(CallerPS))
    {
        if (IsRaidLeader(TargetPS) || IsRaidAssistant(TargetPS)) return false;
    }

    AGroupActor* TargetGroup = FindGroupForPlayer(TargetPS);
    if (!TargetGroup) return false;

    TargetGroup->GetGroupComponent()->KickMember(
        TargetGroup->GetGroupComponent()->GetLeader(), TargetPS);
    // Note: KickMember above requires a group leader as caller; for raid kicks we bypass
    // authority at group level by calling RemoveMember directly and broadcasting Kicked.
    // Implementation choice: expose RemoveMember and broadcast separately, or add an
    // overload that skips group-leader check. See Code Review for recommendation.
    return true;
}
```

> **Implementation note:** The above `KickMember` calls `KickMember` on `UGroupComponent` with the group's own leader as the caller, which works correctly only if the kicked player is in a group whose leader is available. A cleaner approach is to add `UGroupComponent::ForceRemoveMember(APlayerState*)` (authority-bypass, server-only) for the raid kick path.

---

## AssignRaidRoles

Recomputes `EGroupRaidRole` for every member across all groups. Called whenever raid composition or leadership changes.

```cpp
void URaidComponent::AssignRaidRoles()
{
    for (AGroupActor* Group : Groups)
    {
        if (!Group) continue;
        UGroupComponent* GC = Group->GetGroupComponent();

        TArray<APlayerState*> Members;
        GC->GetAllMembers(Members);

        for (APlayerState* PS : Members)
        {
            EGroupRaidRole Role = EGroupRaidRole::Member;
            if      (IsRaidLeader(PS))          Role = EGroupRaidRole::RaidLeader;
            else if (IsRaidAssistant(PS))        Role = EGroupRaidRole::Assistant;
            else if (GC->GetLeader() == PS)      Role = EGroupRaidRole::GroupLeader;

            GC->SetRaidRoleForMember(PS, Role);
        }
    }
}
```

---

## TransferRaidLeadershipToNextGroup

```cpp
void URaidComponent::TransferRaidLeadershipToNextGroup()
{
    APlayerState* OldLeader = RaidLeaderPS.Get();
    RaidLeaderPS = nullptr;

    for (AGroupActor* Group : Groups)
    {
        if (!Group) continue;
        if (APlayerState* GL = Group->GetGroupComponent()->GetLeader())
        {
            RaidLeaderPS = GL;
            AssignRaidRoles();
            BroadcastRaidViewToAllGroups();

            UGameCoreEventBus::Get(this)->Broadcast(
                GameCoreGroupTags::Raid_LeaderChanged,
                FInstancedStruct::Make(FRaidLeaderChangedMessage{ GetOwner<ARaidActor>(), GL, OldLeader }),
                EGameCoreEventScope::ServerOnly);
            return;
        }
    }
    // No valid leader found — raid will dissolve when next group removes itself.
}
```

---

## PopulateRaidMemberEntries

```cpp
void URaidComponent::PopulateRaidMemberEntries(
    const AGroupActor* ForGroup,
    TArray<FGroupMemberEntry>& OutRaidMembers) const
{
    OutRaidMembers.Reset();
    for (const AGroupActor* Group : Groups)
    {
        if (!Group || Group == ForGroup) continue;

        TArray<APlayerState*> Members;
        Group->GetGroupComponent()->GetAllMembers(Members);

        for (APlayerState* PS : Members)
        {
            FGroupMemberEntry Entry;
            Entry.MemberPlayerState = PS;
            Entry.UniqueNetIdString  = PS->GetUniqueId().ToString();
            Entry.SlotIndex          = Group->GetGroupComponent()->GetSlotIndex(PS);
            Entry.bIsGroupLeader     = (Group->GetGroupComponent()->GetLeader() == PS);
            Entry.RaidRole           = Group->GetGroupComponent()->GetRaidRole(PS);
            // GroupSharedData is intentionally null for cross-group entries.
            Entry.RaidSharedData     = Group->GetGroupComponent()->GetRaidSharedData(PS);
            OutRaidMembers.Add(MoveTemp(Entry));
        }
    }
}
```

---

## BroadcastRaidViewToAllGroups

```cpp
void URaidComponent::BroadcastRaidViewToAllGroups()
{
    for (AGroupActor* Group : Groups)
    {
        if (Group)
            Group->GetGroupComponent()->PushViewToAllMembers();
    }
}
```

---

## GetGroupIndex

```cpp
int32 URaidComponent::GetGroupIndex(const AGroupActor* Group) const
{
    return Groups.IndexOfByKey(Group);
}
```
