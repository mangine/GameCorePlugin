# UGroupSubsystem

**File:** `Group/GroupSubsystem.h` / `GroupSubsystem.cpp`
**Type:** `UWorldSubsystem`
**Authority:** Server only

Central registry and lifecycle manager for all active groups and raids. Spawns and destroys `AGroupActor` and `ARaidActor`. Manages pending invites, disconnect grace timers, and reconnect matching. All other systems go through this subsystem to locate groups by player.

`AGameMode` must call `OnPlayerDisconnected` and `OnPlayerReconnected` explicitly — the subsystem does not hook `AGameMode` directly (avoids a hard module dependency).

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UGroupSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    // ── Lifecycle hooks — call from AGameMode ───────────────────────────────

    // Call from AGameMode::NotifyPlayerDisconnected.
    // Transfers leadership immediately if needed, starts grace timer.
    void OnPlayerDisconnected(APlayerState* PS);

    // Call from AGameMode::PostLogin when a player logs in.
    // Matches by UniqueNetId. Cancels grace timer and restores slot if found.
    // No-op if the player is not in a grace window (fresh login).
    void OnPlayerReconnected(APlayerState* PS);

    // ── Group management ────────────────────────────────────────────────────

    // Creates a new AGroupActor with LeaderPS as first member and leader.
    // Returns nullptr if LeaderPS is already in a group.
    AGroupActor* CreateGroup(APlayerState* LeaderPS);

    // Destroys the group actor and clears GroupView on all remaining members.
    // Called automatically by UGroupComponent when group size drops to 1.
    void DisbandGroup(AGroupActor* Group);

    // Returns the AGroupActor this player currently belongs to, or nullptr.
    AGroupActor* FindGroupForPlayer(const APlayerState* PS) const;

    // Returns the ARaidActor this player currently belongs to, or nullptr.
    ARaidActor* FindRaidForPlayer(const APlayerState* PS) const;

    // ── Invite management ───────────────────────────────────────────────────

    // Creates a FPendingGroupInvite. Replaces any existing invite to the same target.
    // Returns false if validation fails (inviter not a leader, group full,
    // target already in a group, target is the inviter, etc.).
    bool CreateInvite(APlayerState* InviterPS, APlayerState* TargetPS);

    // Creates a raid invite targeting a group leader.
    // Returns false if RaidLeaderPS is not the raid leader, or raid is full.
    bool CreateRaidInvite(APlayerState* RaidLeaderPS, APlayerState* TargetGroupLeaderPS);

    // Accepts the pending invite for TargetPS.
    // Returns false if no valid invite exists or inviter has left.
    bool AcceptInvite(APlayerState* TargetPS);

    // Declines or cancels the pending invite for TargetPS.
    void DeclineInvite(APlayerState* TargetPS);

    // ── Raid management ─────────────────────────────────────────────────────

    // Creates a new ARaidActor with InitiatingGroup as the first group.
    // The leader of InitiatingGroup becomes the initial raid leader.
    ARaidActor* CreateRaid(AGroupActor* InitiatingGroup);

    // Removes a group from the raid. Pass nullptr Group to force-dissolve.
    // Destroys ARaidActor if only one group remains after removal.
    void RemoveGroupFromRaid(AGroupActor* Group, ARaidActor* Raid);

protected:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

private:
    // All active groups.
    UPROPERTY()
    TArray<TObjectPtr<AGroupActor>> ActiveGroups;

    // All active raids.
    UPROPERTY()
    TArray<TObjectPtr<ARaidActor>> ActiveRaids;

    // Pending invites. Only one invite per target at a time.
    // Key = UniqueNetId string of the target.
    TMap<FString, FPendingGroupInvite> PendingInvites;

    // Disconnect grace entries. Key = UniqueNetId string of the disconnected player.
    TMap<FString, FDisconnectGraceEntry> GraceEntries;

    // Fast lookup: UniqueNetId string → AGroupActor (weak to handle stale actors).
    TMap<FString, TWeakObjectPtr<AGroupActor>> PlayerToGroup;

    // Repeating timer handle for invite expiry polling (fires every 1 s).
    FTimerHandle InviteExpiryTimerHandle;

    const UGroupConfigDataAsset* GetConfig() const;
    void TickInviteExpiry();
    void OnGraceExpired(FString UniqueNetIdString);
};
```

---

## ShouldCreateSubsystem

```cpp
bool UGroupSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // Only create on the server. Never run on a client.
    UWorld* World = Cast<UWorld>(Outer);
    return World && World->GetNetMode() != NM_Client;
}
```

---

## OnWorldBeginPlay

```cpp
void UGroupSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    // Start invite expiry polling. Precision of 1 s is sufficient.
    GetWorld()->GetTimerManager().SetTimer(
        InviteExpiryTimerHandle,
        this, &UGroupSubsystem::TickInviteExpiry,
        1.0f, /*bLoop=*/true);
}
```

---

## Deinitialize

```cpp
void UGroupSubsystem::Deinitialize()
{
    GetWorld()->GetTimerManager().ClearTimer(InviteExpiryTimerHandle);
    // Clear all grace timers.
    for (auto& Pair : GraceEntries)
        GetWorld()->GetTimerManager().ClearTimer(Pair.Value.TimerHandle);
    GraceEntries.Empty();
    PendingInvites.Empty();
    Super::Deinitialize();
}
```

---

## CreateGroup

```cpp
AGroupActor* UGroupSubsystem::CreateGroup(APlayerState* LeaderPS)
{
    if (!LeaderPS) return nullptr;
    if (FindGroupForPlayer(LeaderPS)) return nullptr; // already in a group

    FActorSpawnParameters Params;
    Params.Owner = nullptr;
    AGroupActor* Group = GetWorld()->SpawnActor<AGroupActor>(AGroupActor::StaticClass(), Params);
    check(Group);

    ActiveGroups.Add(Group);
    Group->GetGroupComponent()->AddMember(LeaderPS);
    PlayerToGroup.Add(LeaderPS->GetUniqueId().ToString(), Group);

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::Formed,
        FInstancedStruct::Make(FGroupMemberChangedMessage{ LeaderPS, Group }),
        EGameCoreEventScope::ServerOnly);

    return Group;
}
```

---

## DisbandGroup

```cpp
void UGroupSubsystem::DisbandGroup(AGroupActor* Group)
{
    if (!Group) return;

    // Remove from any raid first.
    ARaidActor* Raid = FindRaidForPlayer(
        Group->GetGroupComponent()->GetLeader());
    if (Raid)
        RemoveGroupFromRaid(Group, Raid);

    // Clear GroupView on all remaining members.
    TArray<APlayerState*> Members;
    Group->GetGroupComponent()->GetAllMembers(Members);
    for (APlayerState* PS : Members)
    {
        if (UGroupMemberComponent* MC = PS->FindComponentByClass<UGroupMemberComponent>())
            MC->SetGroupView(FGroupMemberView{});
        PlayerToGroup.Remove(PS->GetUniqueId().ToString());
    }

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::Disbanded,
        FInstancedStruct::Make(FGroupDisbandedMessage{ Group }),
        EGameCoreEventScope::ServerOnly);

    ActiveGroups.Remove(Group);
    Group->Destroy();
}
```

---

## FindGroupForPlayer

```cpp
AGroupActor* UGroupSubsystem::FindGroupForPlayer(const APlayerState* PS) const
{
    if (!PS) return nullptr;
    const TWeakObjectPtr<AGroupActor>* Found =
        PlayerToGroup.Find(PS->GetUniqueId().ToString());
    return Found ? Found->Get() : nullptr;
}
```

---

## FindRaidForPlayer

```cpp
ARaidActor* UGroupSubsystem::FindRaidForPlayer(const APlayerState* PS) const
{
    AGroupActor* Group = FindGroupForPlayer(PS);
    if (!Group) return nullptr;
    // AGroupActor exposes OwningRaid via a public getter.
    return Group->GetOwningRaid();
}
```

---

## OnPlayerDisconnected

```cpp
void UGroupSubsystem::OnPlayerDisconnected(APlayerState* PS)
{
    if (!PS) return;
    AGroupActor* Group = FindGroupForPlayer(PS);
    if (!Group) return;

    UGroupComponent* GC = Group->GetGroupComponent();
    FString NetId = PS->GetUniqueId().ToString();

    // Transfer leadership before marking disconnected so the new leader
    // receives the view with correct bIsGroupLeader flags.
    if (GC->GetLeader() == PS)
    {
        GC->TransferLeadershipToNextSlot();
        // Fire LeaderChanged event after transfer.
        UGameCoreEventBus::Get(this)->Broadcast(
            GameCoreGroupTags::LeaderChanged,
            FInstancedStruct::Make(FGroupLeaderChangedMessage{ Group, GC->GetLeader(), PS }),
            EGameCoreEventScope::ServerOnly);
    }

    GC->MarkMemberDisconnected(PS);
    GC->PushViewToAllMembers();

    // Start grace timer.
    FDisconnectGraceEntry Entry;
    Entry.Group             = Group;
    Entry.SlotIndex         = GC->GetSlotIndex(PS);
    Entry.UniqueNetIdString = NetId;
    Entry.ExpiryTime        = GetWorld()->GetTimeSeconds() + GetConfig()->DisconnectGraceSeconds;
    GetWorld()->GetTimerManager().SetTimer(
        Entry.TimerHandle,
        FTimerDelegate::CreateUObject(this, &UGroupSubsystem::OnGraceExpired, NetId),
        GetConfig()->DisconnectGraceSeconds,
        /*bLoop=*/false);
    GraceEntries.Add(NetId, MoveTemp(Entry));

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::MemberDisconnected,
        FInstancedStruct::Make(FGroupMemberChangedMessage{ PS, Group }),
        EGameCoreEventScope::ServerOnly);
}
```

---

## OnPlayerReconnected

```cpp
void UGroupSubsystem::OnPlayerReconnected(APlayerState* PS)
{
    if (!PS) return;
    FString NetId = PS->GetUniqueId().ToString();

    FDisconnectGraceEntry* Entry = GraceEntries.Find(NetId);
    if (!Entry) return; // no grace entry — fresh login

    AGroupActor* Group = Entry->Group.Get();
    if (!Group)
    {
        // Group was disbanded during grace window.
        GraceEntries.Remove(NetId);
        return;
    }

    GetWorld()->GetTimerManager().ClearTimer(Entry->TimerHandle);
    uint8 Slot = Entry->SlotIndex;
    GraceEntries.Remove(NetId);

    UGroupComponent* GC = Group->GetGroupComponent();
    GC->RestoreMember(PS, Slot);
    GC->PushViewToAllMembers();

    PlayerToGroup.Add(NetId, Group);

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::MemberReconnected,
        FInstancedStruct::Make(FGroupMemberChangedMessage{ PS, Group }),
        EGameCoreEventScope::ServerOnly);
}
```

---

## CreateInvite

```cpp
bool UGroupSubsystem::CreateInvite(APlayerState* InviterPS, APlayerState* TargetPS)
{
    if (!InviterPS || !TargetPS || InviterPS == TargetPS) return false;

    // Target must not already be in a group.
    if (FindGroupForPlayer(TargetPS)) return false;

    AGroupActor* Group = FindGroupForPlayer(InviterPS);

    // Inviter must be the group leader (or have no group yet, meaning they become leader on accept).
    if (Group && Group->GetGroupComponent()->GetLeader() != InviterPS) return false;

    // Enforce max size (account for already-pending invites is a nice-to-have, not enforced here).
    if (Group && Group->GetGroupComponent()->GetMemberCount() >= GetConfig()->MaxGroupSize)
        return false;

    FString TargetNetId = TargetPS->GetUniqueId().ToString();
    FPendingGroupInvite Invite;
    Invite.InviterPS    = InviterPS;
    Invite.TargetPS     = TargetPS;
    Invite.ExpiryTime   = GetWorld()->GetTimeSeconds() + GetConfig()->InviteExpirySeconds;
    Invite.bIsRaidInvite = false;
    PendingInvites.Add(TargetNetId, MoveTemp(Invite));

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::InviteReceived,
        FInstancedStruct::Make(FGroupInviteMessage{ InviterPS, TargetPS, false }),
        EGameCoreEventScope::ServerOnly);
    return true;
}
```

---

## AcceptInvite

```cpp
bool UGroupSubsystem::AcceptInvite(APlayerState* TargetPS)
{
    FString NetId = TargetPS->GetUniqueId().ToString();
    FPendingGroupInvite* Invite = PendingInvites.Find(NetId);
    if (!Invite) return false;

    APlayerState* InviterPS = Invite->InviterPS.Get();
    if (!InviterPS)
    {
        PendingInvites.Remove(NetId);
        return false;
    }

    if (Invite->bIsRaidInvite)
    {
        ARaidActor* Raid   = FindRaidForPlayer(InviterPS);
        AGroupActor* TgtGrp = FindGroupForPlayer(TargetPS);
        if (!Raid || !TgtGrp) { PendingInvites.Remove(NetId); return false; }
        Raid->GetRaidComponent()->AddGroup(TgtGrp);
    }
    else
    {
        AGroupActor* Group = FindGroupForPlayer(InviterPS);
        if (!Group)
            Group = CreateGroup(InviterPS);

        if (!Group->GetGroupComponent()->AddMember(TargetPS))
        {
            PendingInvites.Remove(NetId);
            return false;
        }
        PlayerToGroup.Add(NetId, Group);
    }

    PendingInvites.Remove(NetId);
    return true;
}
```

---

## DeclineInvite

```cpp
void UGroupSubsystem::DeclineInvite(APlayerState* TargetPS)
{
    FString NetId = TargetPS->GetUniqueId().ToString();
    FPendingGroupInvite* Invite = PendingInvites.Find(NetId);
    if (!Invite) return;

    APlayerState* InviterPS = Invite->InviterPS.Get();
    PendingInvites.Remove(NetId);

    if (InviterPS)
        UGameCoreEventBus::Get(this)->Broadcast(
            GameCoreGroupTags::InviteDeclined,
            FInstancedStruct::Make(FGroupInviteMessage{ InviterPS, TargetPS, false }),
            EGameCoreEventScope::ServerOnly);
}
```

---

## TickInviteExpiry

Called every 1 second via repeating timer set in `OnWorldBeginPlay`.

```cpp
void UGroupSubsystem::TickInviteExpiry()
{
    float Now = GetWorld()->GetTimeSeconds();
    for (auto It = PendingInvites.CreateIterator(); It; ++It)
    {
        if (Now >= It->Value.ExpiryTime)
        {
            APlayerState* Inviter = It->Value.InviterPS.Get();
            APlayerState* Target  = It->Value.TargetPS.Get();
            bool bRaid = It->Value.bIsRaidInvite;
            It.RemoveCurrent();

            FGroupInviteMessage Msg;
            Msg.InviterPS    = Inviter;
            Msg.TargetPS     = Target;
            Msg.bIsRaidInvite = bRaid;

            UGameCoreEventBus::Get(this)->Broadcast(
                GameCoreGroupTags::InviteExpired,
                FInstancedStruct::Make(Msg),
                EGameCoreEventScope::ServerOnly);
        }
    }
}
```

---

## OnGraceExpired

```cpp
void UGroupSubsystem::OnGraceExpired(FString UniqueNetIdString)
{
    FDisconnectGraceEntry* Entry = GraceEntries.Find(UniqueNetIdString);
    if (!Entry) return;

    AGroupActor* Group = Entry->Group.Get();
    GraceEntries.Remove(UniqueNetIdString);
    PlayerToGroup.Remove(UniqueNetIdString);

    if (Group)
        Group->GetGroupComponent()->RemoveMemberByNetId(UniqueNetIdString);
    // RemoveMember triggers standard member-left flow (disband if count drops to 1,
    // PushViewToAllMembers, Broadcast MemberLeft).
}
```

**Note:** `UGroupComponent` must expose `RemoveMemberByNetId(const FString&)` to handle the case where the `APlayerState` pointer is no longer valid after expiry.
