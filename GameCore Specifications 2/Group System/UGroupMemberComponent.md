# UGroupMemberComponent

**File:** `Group/GroupMemberComponent.h` / `GroupMemberComponent.cpp`
**Type:** `UActorComponent` on `APlayerState`
**Authority:** Server mutates; `FGroupMemberView` replicates `COND_OwnerOnly` to owning client.

Player-facing surface of the Group System. Lives on every `APlayerState`. Owns the replicated `FGroupMemberView` snapshot and is the RPC entry point for all player-initiated group actions. Clients never call `AGroupActor` or `URaidComponent` directly — everything routes through RPCs on this component.

Also provides `GroupProvider_*` forwarding methods used when `APlayerState` implements `IGroupProvider`.

---

## Class Declaration

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UGroupMemberComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Client-readable state ──────────────────────────────────────────────

    // Current group/raid snapshot. Updated server-side by UGroupComponent::PushViewToAllMembers.
    // Replicates COND_OwnerOnly.
    UPROPERTY(ReplicatedUsing=OnRep_GroupView, BlueprintReadOnly, Category="Group")
    FGroupMemberView GroupView;

    UFUNCTION()
    void OnRep_GroupView();

    const FGroupMemberView& GetGroupView() const { return GroupView; }

    // Fired on the client whenever GroupView is updated via replication.
    UPROPERTY(BlueprintAssignable, Category="Group")
    FOnGroupViewChanged OnGroupViewChanged;

    // ── Server-side write ──────────────────────────────────────────────────

    // Called server-side by UGroupComponent::PushViewToAllMembers.
    // Overwrites GroupView and marks the property dirty for replication.
    void SetGroupView(FGroupMemberView NewView);

    // ── RPCs — Group ───────────────────────────────────────────────────────

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_InvitePlayer(APlayerState* TargetPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_AcceptInvite();

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_DeclineInvite();

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_LeaveGroup();

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_KickMember(APlayerState* TargetPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_TransferLeadership(APlayerState* NewLeaderPS);

    // ── RPCs — Raid ────────────────────────────────────────────────────────

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_InviteGroupToRaid(APlayerState* TargetGroupLeaderPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_AcceptRaidInvite();

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_DeclineRaidInvite();

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_LeaveRaid();

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_KickMemberFromRaid(APlayerState* TargetPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_TransferRaidLeadership(APlayerState* NewRaidLeaderPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_PromoteToRaidAssistant(APlayerState* TargetPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_DemoteRaidAssistant(APlayerState* TargetPS);

    // ── IGroupProvider forwarding ──────────────────────────────────────────
    // Called by APlayerState when it implements IGroupProvider.

    int32   GroupProvider_GetGroupSize()  const;
    bool    GroupProvider_IsGroupLeader() const;
    void    GroupProvider_GetGroupMembers(TArray<APlayerState*>& Out) const;
    AActor* GroupProvider_GetGroupActor() const;
    AActor* GroupProvider_GetRaidActor()  const;
    void    GroupProvider_GetRaidMembers(TArray<APlayerState*>& Out) const;

protected:
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    UGroupSubsystem* GetSubsystem() const;
};
```

---

## Delegate Declaration

Declared in `GroupMemberComponent.h`:

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGroupViewChanged, const FGroupMemberView&, NewView);
```

---

## GetLifetimeReplicatedProps

```cpp
void UGroupMemberComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UGroupMemberComponent, GroupView, COND_OwnerOnly);
}
```

---

## OnRep_GroupView

```cpp
void UGroupMemberComponent::OnRep_GroupView()
{
    OnGroupViewChanged.Broadcast(GroupView);
}
```

---

## SetGroupView

```cpp
void UGroupMemberComponent::SetGroupView(FGroupMemberView NewView)
{
    GroupView = MoveTemp(NewView);
    MARK_PROPERTY_DIRTY_FROM_NAME(UGroupMemberComponent, GroupView, this);
}
```

> Uses Push Model replication (`MARK_PROPERTY_DIRTY_FROM_NAME`) for efficiency — the property is only sent when explicitly marked dirty. Requires enabling `WITH_PUSH_MODEL` and listing the component in the push model include.

---

## RPC Implementations

### ServerRPC_InvitePlayer

```cpp
bool UGroupMemberComponent::ServerRPC_InvitePlayer_Validate(APlayerState* TargetPS)
{
    return TargetPS != nullptr && TargetPS != GetOwner();
}

void UGroupMemberComponent::ServerRPC_InvitePlayer_Implementation(APlayerState* TargetPS)
{
    GetSubsystem()->CreateInvite(GetOwner<APlayerState>(), TargetPS);
}
```

### ServerRPC_AcceptInvite

```cpp
bool UGroupMemberComponent::ServerRPC_AcceptInvite_Validate() { return true; }

void UGroupMemberComponent::ServerRPC_AcceptInvite_Implementation()
{
    GetSubsystem()->AcceptInvite(GetOwner<APlayerState>());
}
```

### ServerRPC_DeclineInvite

```cpp
bool UGroupMemberComponent::ServerRPC_DeclineInvite_Validate() { return true; }

void UGroupMemberComponent::ServerRPC_DeclineInvite_Implementation()
{
    GetSubsystem()->DeclineInvite(GetOwner<APlayerState>());
}
```

### ServerRPC_LeaveGroup

```cpp
bool UGroupMemberComponent::ServerRPC_LeaveGroup_Validate() { return true; }

void UGroupMemberComponent::ServerRPC_LeaveGroup_Implementation()
{
    APlayerState* SelfPS = GetOwner<APlayerState>();
    AGroupActor* Group   = GetSubsystem()->FindGroupForPlayer(SelfPS);
    if (!Group) return;

    // If in a raid and this player is the group leader, remove the group from the raid first.
    ARaidActor* Raid = GetSubsystem()->FindRaidForPlayer(SelfPS);
    if (Raid && Group->GetGroupComponent()->GetLeader() == SelfPS)
        Raid->GetRaidComponent()->RemoveGroup(Group);

    Group->GetGroupComponent()->RemoveMember(SelfPS);
}
```

### ServerRPC_KickMember

```cpp
bool UGroupMemberComponent::ServerRPC_KickMember_Validate(APlayerState* TargetPS)
{
    return TargetPS != nullptr && TargetPS != GetOwner();
}

void UGroupMemberComponent::ServerRPC_KickMember_Implementation(APlayerState* TargetPS)
{
    AGroupActor* Group = GetSubsystem()->FindGroupForPlayer(GetOwner<APlayerState>());
    if (!Group) return;
    Group->GetGroupComponent()->KickMember(GetOwner<APlayerState>(), TargetPS);
}
```

### ServerRPC_TransferLeadership

```cpp
bool UGroupMemberComponent::ServerRPC_TransferLeadership_Validate(APlayerState* NewLeaderPS)
{
    return NewLeaderPS != nullptr && NewLeaderPS != GetOwner();
}

void UGroupMemberComponent::ServerRPC_TransferLeadership_Implementation(APlayerState* NewLeaderPS)
{
    AGroupActor* Group = GetSubsystem()->FindGroupForPlayer(GetOwner<APlayerState>());
    if (!Group) return;
    if (Group->GetGroupComponent()->GetLeader() != GetOwner<APlayerState>()) return;
    Group->GetGroupComponent()->TransferLeadership(NewLeaderPS);
}
```

### ServerRPC_InviteGroupToRaid

```cpp
bool UGroupMemberComponent::ServerRPC_InviteGroupToRaid_Validate(APlayerState* TargetGroupLeaderPS)
{
    return TargetGroupLeaderPS != nullptr && TargetGroupLeaderPS != GetOwner();
}

void UGroupMemberComponent::ServerRPC_InviteGroupToRaid_Implementation(APlayerState* TargetGroupLeaderPS)
{
    GetSubsystem()->CreateRaidInvite(GetOwner<APlayerState>(), TargetGroupLeaderPS);
}
```

### ServerRPC_LeaveRaid

```cpp
bool UGroupMemberComponent::ServerRPC_LeaveRaid_Validate() { return true; }

void UGroupMemberComponent::ServerRPC_LeaveRaid_Implementation()
{
    APlayerState* SelfPS = GetOwner<APlayerState>();
    ARaidActor* Raid     = GetSubsystem()->FindRaidForPlayer(SelfPS);
    AGroupActor* Group   = GetSubsystem()->FindGroupForPlayer(SelfPS);
    if (!Raid || !Group) return;
    // Only the group leader can remove their group from a raid.
    if (Group->GetGroupComponent()->GetLeader() != SelfPS) return;
    Raid->GetRaidComponent()->RemoveGroup(Group);
}
```

---

## IGroupProvider Forwarding

```cpp
int32 UGroupMemberComponent::GroupProvider_GetGroupSize() const
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        AGroupActor* Group = GetSubsystem()->FindGroupForPlayer(GetOwner<APlayerState>());
        return Group ? Group->GetGroupComponent()->GetMemberCount() : 1;
    }
    return FMath::Max(1, GroupView.Members.Num());
}

bool UGroupMemberComponent::GroupProvider_IsGroupLeader() const
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        AGroupActor* Group = GetSubsystem()->FindGroupForPlayer(GetOwner<APlayerState>());
        return Group && Group->GetGroupComponent()->GetLeader() == GetOwner<APlayerState>();
    }
    // Client fallback — read from replicated view.
    APlayerState* Self = GetOwner<APlayerState>();
    for (const FGroupMemberEntry& E : GroupView.Members)
        if (E.MemberPlayerState == Self) return E.bIsGroupLeader;
    return false;
}

void UGroupMemberComponent::GroupProvider_GetGroupMembers(TArray<APlayerState*>& Out) const
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        AGroupActor* Group = GetSubsystem()->FindGroupForPlayer(GetOwner<APlayerState>());
        if (Group) { Group->GetGroupComponent()->GetAllMembers(Out); return; }
    }
    for (const FGroupMemberEntry& E : GroupView.Members)
        if (E.MemberPlayerState) Out.Add(E.MemberPlayerState.Get());
}

AActor* UGroupMemberComponent::GroupProvider_GetGroupActor() const
{
    if (GetOwnerRole() != ROLE_Authority) return nullptr;
    return GetSubsystem()->FindGroupForPlayer(GetOwner<APlayerState>());
}

AActor* UGroupMemberComponent::GroupProvider_GetRaidActor() const
{
    if (GetOwnerRole() != ROLE_Authority) return nullptr;
    return GetSubsystem()->FindRaidForPlayer(GetOwner<APlayerState>());
}

void UGroupMemberComponent::GroupProvider_GetRaidMembers(TArray<APlayerState*>& Out) const
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        ARaidActor* Raid = GetSubsystem()->FindRaidForPlayer(GetOwner<APlayerState>());
        if (Raid) { Raid->GetRaidComponent()->GetAllRaidMembers(Out); return; }
    }
    // Client fallback — read RaidMembers from replicated view.
    for (const FGroupMemberEntry& E : GroupView.RaidMembers)
        if (E.MemberPlayerState) Out.Add(E.MemberPlayerState.Get());
}
```

---

## GetSubsystem

```cpp
UGroupSubsystem* UGroupMemberComponent::GetSubsystem() const
{
    // Never cache — UWorldSubsystem lifetime is managed by the engine.
    return GetWorld()->GetSubsystem<UGroupSubsystem>();
}
```

> Do not store a cached pointer to `UGroupSubsystem`. Always call `GetSubsystem()` inline at RPC call sites.
