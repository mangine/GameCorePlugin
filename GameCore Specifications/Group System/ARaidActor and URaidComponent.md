# ARaidActor and URaidComponent

**Sub-page of:** [Group System](../Group%20System.md)
**Files:** `Group/RaidActor.h / .cpp`, `Group/RaidComponent.h / .cpp`
**Authority:** Server only

`ARaidActor` is the server-side container for a multi-group raid. It holds references to the constituent `AGroupActor` instances and manages the raid leader / assistant authority model. It does not own quest or shared data directly — those live on the individual group actors.

---

## ARaidActor

```cpp
ACLASS(NotBlueprintable)
class GAMECORE_API ARaidActor : public AActor
{
    GENERATED_BODY()
public:
    ARaidActor();

    URaidComponent* GetRaidComponent() const { return RaidComponent; }

private:
    UPROPERTY()
    TObjectPtr<URaidComponent> RaidComponent;
};

ARaidActor::ARaidActor()
{
    bReplicates = false;
    bNetLoadOnClient = false;
    PrimaryActorTick.bCanEverTick = false;
    RaidComponent = CreateDefaultSubobject<URaidComponent>(TEXT("RaidComponent"));
}
```

---

## URaidComponent

```cpp
UCLASS()
class GAMECORE_API URaidComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Group management ─────────────────────────────────────────────────

    // Adds a group to the raid. Group leader becomes a GroupLeader raid role.
    // Returns false if raid is already at MaxGroupsPerRaid.
    bool AddGroup(AGroupActor* Group);

    // Removes a group. If the raid leader belonged to it, transfer raid leadership.
    // If only one group remains after removal, dissolve the raid.
    void RemoveGroup(AGroupActor* Group);

    // ── Raid leadership ───────────────────────────────────────────────────

    // The single authoritative raid leader.
    APlayerState* GetRaidLeader() const;

    // Transfers raid leadership from current leader to NewLeader.
    // NewLeader must currently be a group leader within the raid.
    // Returns false if NewLeader is not eligible.
    bool TransferRaidLeadership(APlayerState* CallerPS, APlayerState* NewLeader);

    // Promotes TargetPS to raid assistant.
    // Only the raid leader can call this.
    bool PromoteToAssistant(APlayerState* CallerPS, APlayerState* TargetPS);

    // Demotes TargetPS from raid assistant to member.
    // Only the raid leader can call this.
    bool DemoteAssistant(APlayerState* CallerPS, APlayerState* TargetPS);

    // ── Kick ─────────────────────────────────────────────────────────────

    // Raid leader or any raid assistant can kick any member across all groups.
    // Returns false if CallerPS has insufficient authority.
    bool KickMember(APlayerState* CallerPS, APlayerState* TargetPS);

    // ── Queries ──────────────────────────────────────────────────────────

    int32 GetGroupCount() const;
    bool IsRaidLeader(const APlayerState* PS) const;
    bool IsRaidAssistant(const APlayerState* PS) const;
    void GetAllRaidMembers(TArray<APlayerState*>& Out) const;
    AGroupActor* FindGroupForPlayer(const APlayerState* PS) const;

    // Called by UGroupComponent when it rebuilds views, to obtain cross-group entries.
    void PopulateRaidMemberEntries(
        const AGroupActor* ForGroup,
        TArray<FGroupMemberEntry>& OutRaidMembers) const;

private:
    UPROPERTY()
    TArray<TObjectPtr<AGroupActor>> Groups; // ordered by join time, index = GroupIndexInRaid

    // The single raid leader.
    TWeakObjectPtr<APlayerState> RaidLeaderPS;

    // Raid assistants. Can kick but cannot manage leadership.
    TArray<TWeakObjectPtr<APlayerState>> Assistants;

    const UGroupConfigDataAsset* GetConfig() const;

    void AssignRaidRoles();
    void BroadcastRaidViewToAllGroups();
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

    // Set OwningRaid backref on the group actor.
    Group->OwningRaid = GetOwner<ARaidActor>();

    AssignRaidRoles();
    BroadcastRaidViewToAllGroups();

    // Fire GMS event: GameCoreEvent.Raid.GroupJoined
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

    // If the raid leader was in the leaving group, transfer leadership.
    APlayerState* Leader = RaidLeaderPS.Get();
    if (Leader)
    {
        AGroupActor* LeaderGroup = FindGroupForPlayer(Leader);
        if (!LeaderGroup || LeaderGroup == Group)
            TransferRaidLeadershipToNextGroup();
    }

    // Clean up assistants that were in the leaving group.
    Assistants.RemoveAll([Group](const TWeakObjectPtr<APlayerState>& W)
    {
        APlayerState* PS = W.Get();
        if (!PS) return true;
        // Check if PS was in the removed group (group no longer has PS since it's leaving).
        // The group's member list is still intact at this point.
        return Group->GetGroupComponent()->IsMember(PS);
    });

    if (Groups.Num() <= 1)
    {
        // Dissolve raid — only one group left.
        UGroupSubsystem* GS = GetWorld()->GetSubsystem<UGroupSubsystem>();
        if (Groups.Num() == 1)
            Groups[0]->OwningRaid = nullptr;
        Groups.Empty();
        GS->RemoveGroupFromRaid(nullptr, GetOwner<ARaidActor>());
        return;
    }

    AssignRaidRoles();
    BroadcastRaidViewToAllGroups();
    // Fire GMS event: GameCoreEvent.Raid.GroupLeft
}
```

---

## KickMember (Raid-level)

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

    TargetGroup->GetGroupComponent()->RemoveMember(TargetPS);
    // Fire GMS event: GameCoreEvent.Group.Kicked (on the member's PlayerState connection)
    return true;
}
```

---

## AssignRaidRoles

Recomputes `EGroupRaidRole` for every member across all groups and updates their `FGroupMemberEntry`. Called whenever the raid composition or leadership changes.

```cpp
void URaidComponent::AssignRaidRoles()
{
    for (AGroupActor* Group : Groups)
    {
        if (!Group) continue;
        UGroupComponent* GC = Group->GetGroupComponent();
        // GC->Members is private; access via a friend or a dedicated setter.
        // UGroupComponent exposes SetRaidRoleForMember(APlayerState*, EGroupRaidRole).
        for (APlayerState* PS : TArray<APlayerState*>{})
        {
            // Build per-member role.
            EGroupRaidRole Role = EGroupRaidRole::Member;
            if (IsRaidLeader(PS))         Role = EGroupRaidRole::RaidLeader;
            else if (IsRaidAssistant(PS)) Role = EGroupRaidRole::Assistant;
            else if (GC->GetLeader() == PS) Role = EGroupRaidRole::GroupLeader;

            GC->SetRaidRoleForMember(PS, Role);
        }
    }
}
```

> **Implementation note:** `UGroupComponent` must expose `SetRaidRoleForMember(APlayerState*, EGroupRaidRole)` as a friend-accessible or public method. `AssignRaidRoles` iterates by calling `GetAllMembers()` on each group component.

---

## TransferRaidLeadershipToNextGroup

```cpp
void URaidComponent::TransferRaidLeadershipToNextGroup()
{
    RaidLeaderPS = nullptr;
    for (AGroupActor* Group : Groups)
    {
        if (!Group) continue;
        APlayerState* GroupLeader = Group->GetGroupComponent()->GetLeader();
        if (GroupLeader)
        {
            RaidLeaderPS = GroupLeader;
            AssignRaidRoles();
            BroadcastRaidViewToAllGroups();
            // Fire GMS event: GameCoreEvent.Raid.LeaderChanged
            return;
        }
    }
    // No valid leader found — raid will dissolve on next group removal.
}
```

---

## PopulateRaidMemberEntries

Called by `UGroupComponent::BuildViewForMember` to fill `FGroupMemberView::RaidMembers` with cross-group entries.

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
            uint8 Slot = Group->GetGroupComponent()->GetSlotIndex(PS);
            FGroupMemberEntry Entry;
            Entry.MemberPlayerState = PS;
            Entry.UniqueNetIdString  = PS->GetUniqueId().ToString();
            Entry.SlotIndex          = Slot;
            Entry.bIsGroupLeader     = (Group->GetGroupComponent()->GetLeader() == PS);
            Entry.RaidRole           = /* read from stored role */;
            // GroupSharedData is intentionally left null for cross-group entries.
            // RaidSharedData is set from the member's FGroupMemberEntry in their own group.
            // UGroupComponent must expose GetRaidSharedData(APlayerState*) for this.
            Entry.RaidSharedData = Group->GetGroupComponent()->GetRaidSharedData(PS);
            OutRaidMembers.Add(MoveTemp(Entry));
        }
    }
}
```
