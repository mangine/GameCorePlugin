# UGroupSubsystem

**Sub-page of:** [Group System](../Group%20System.md)
**File:** `Group/GroupSubsystem.h / .cpp`
**Type:** `UWorldSubsystem`
**Authority:** Server only

Central registry and lifecycle manager for all active groups and raids. Spawns and destroys `AGroupActor` and `ARaidActor`. Manages pending invites, disconnect grace timers, and reconnect matching. All other systems go through this subsystem to locate groups by player.

`AGameMode` must call `OnPlayerDisconnected` and `OnPlayerReconnected` explicitly — the subsystem does not hook `AGameMode` directly.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UGroupSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    // ── Lifecycle hooks — call from AGameMode ──────────────────────────────

    // Call from AGameMode::NotifyPlayerDisconnected.
    // Starts the grace timer. Transfers leadership immediately if needed.
    void OnPlayerDisconnected(APlayerState* PS);

    // Call from AGameMode::PostLogin when a player reconnects.
    // Matches by UniqueNetId. Cancels grace timer and restores slot if found.
    void OnPlayerReconnected(APlayerState* PS);

    // ── Group management ───────────────────────────────────────────────────

    // Creates a new AGroupActor with LeaderPS as first member and leader.
    // Returns nullptr if LeaderPS is already in a group.
    AGroupActor* CreateGroup(APlayerState* LeaderPS);

    // Destroys the group actor and notifies all members via FGroupMemberView.
    // Called automatically when group size reaches 0.
    void DisbandGroup(AGroupActor* Group);

    // Returns the AGroupActor this player currently belongs to, or nullptr.
    AGroupActor* FindGroupForPlayer(const APlayerState* PS) const;

    // Returns the ARaidActor this player currently belongs to, or nullptr.
    ARaidActor* FindRaidForPlayer(const APlayerState* PS) const;

    // ── Invite management ─────────────────────────────────────────────────

    // Creates a FPendingGroupInvite. Replaces any existing invite to the same target.
    // Returns false if validation fails (inviter not a leader, group full, etc.).
    bool CreateInvite(APlayerState* InviterPS, APlayerState* TargetPS);

    // Creates a raid invite targeting a group leader.
    // Returns false if RaidLeaderPS is not the raid leader or raid is full.
    bool CreateRaidInvite(APlayerState* RaidLeaderPS, APlayerState* TargetGroupLeaderPS);

    // Accepts the pending invite for TargetPS.
    // Returns false if no valid invite exists.
    bool AcceptInvite(APlayerState* TargetPS);

    // Declines or cancels the pending invite for TargetPS.
    void DeclineInvite(APlayerState* TargetPS);

    // ── Raid management ────────────────────────────────────────────────────

    // Creates a new ARaidActor with InitiatingGroup as the first group.
    // The leader of InitiatingGroup becomes the initial raid leader.
    ARaidActor* CreateRaid(AGroupActor* InitiatingGroup);

    // Removes a group from the raid. Destroys ARaidActor if only one group remains.
    void RemoveGroupFromRaid(AGroupActor* Group, ARaidActor* Raid);

protected:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
    // All active groups, keyed by the AGroupActor pointer.
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

    const UGroupConfigDataAsset* GetConfig() const;

    void TickInviteExpiry();
    void OnGraceExpired(FString UniqueNetIdString);

    // Fast lookup: UniqueNetId string → AGroupActor.
    TMap<FString, TWeakObjectPtr<AGroupActor>> PlayerToGroup;
};
```

---

## ShouldCreateSubsystem

```cpp
bool UGroupSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // Only create on the server. UGroupSubsystem must never run on a client.
    UWorld* World = Cast<UWorld>(Outer);
    return World && World->GetNetMode() != NM_Client;
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
    // receives the updated view with correct bIsGroupLeader flags.
    if (GC->GetLeader() == PS)
        GC->TransferLeadershipToNextSlot();

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
        false);

    GraceEntries.Add(NetId, MoveTemp(Entry));

    BroadcastGroupEvent(GameCoreGroupTags::MemberDisconnected, PS);
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
    if (!Entry) return; // no grace entry — fresh login, not a reconnect

    AGroupActor* Group = Entry->Group.Get();
    if (!Group)
    {
        // Group was disbanded during grace window.
        GraceEntries.Remove(NetId);
        return;
    }

    GetWorld()->GetTimerManager().ClearTimer(Entry->TimerHandle);
    GraceEntries.Remove(NetId);

    UGroupComponent* GC = Group->GetGroupComponent();
    GC->RestoreMember(PS, Entry->SlotIndex);
    GC->PushViewToAllMembers();

    PlayerToGroup.Add(NetId, Group);
    BroadcastGroupEvent(GameCoreGroupTags::MemberReconnected, PS);
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
        // Raid invite: add the target's whole group to the raid.
        ARaidActor* Raid = FindRaidForPlayer(InviterPS);
        AGroupActor* TargetGroup = FindGroupForPlayer(TargetPS);
        if (!Raid || !TargetGroup)
        {
            PendingInvites.Remove(NetId);
            return false;
        }
        Raid->GetRaidComponent()->AddGroup(TargetGroup);
    }
    else
    {
        // Group invite: add target to inviter's group, creating one if needed.
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

## Invite Expiry Tick

Call `TickInviteExpiry` from a repeating timer set up in `OnWorldBeginPlay` (e.g. every 1s — expiry precision does not need to be sub-second).

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
            It.RemoveCurrent();

            if (Inviter) BroadcastGroupEvent(GameCoreGroupTags::InviteExpired, Inviter);
            if (Target)  BroadcastGroupEvent(GameCoreGroupTags::InviteExpired, Target);
        }
    }
}
```
