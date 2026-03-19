# UGroupMemberComponent

**Sub-page of:** [Group System](../Group%20System.md)
**File:** `Group/GroupMemberComponent.h / .cpp`
**Type:** `UActorComponent` on `APlayerState`
**Authority:** Server mutates; `FGroupMemberView` replicates to owning client only

This is the player-facing surface of the Group System. It lives on every `APlayerState`, owns the replicated `FGroupMemberView` snapshot, and is the RPC entry point for all player-initiated group actions. The client never calls `AGroupActor` or `URaidComponent` directly — everything routes through RPCs on this component.

---

## Class Declaration

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UGroupMemberComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Client-readable state ─────────────────────────────────────────────

    // Current group/raid snapshot. Updated by UGroupComponent::PushViewToAllMembers.
    // Replicated COND_OwnerOnly.
    UPROPERTY(ReplicatedUsing=OnRep_GroupView, BlueprintReadOnly, Category="Group")
    FGroupMemberView GroupView;

    UFUNCTION()
    void OnRep_GroupView();

    // Returns the current view. Safe to call on client.
    const FGroupMemberView& GetGroupView() const { return GroupView; }

    // Fired on the client whenever GroupView is updated.
    UPROPERTY(BlueprintAssignable, Category="Group")
    FOnGroupViewChanged OnGroupViewChanged;

    // ── Server entry points (called from server; RPC wrappers call these) ──

    // Overwrites GroupView and marks the property dirty for replication.
    // Called server-side by UGroupComponent::PushViewToAllMembers.
    void SetGroupView(FGroupMemberView&& NewView);

    // ── RPCs — validated and executed server-side ─────────────────────────

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

    // ── Raid RPCs ─────────────────────────────────────────────────────────

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_InviteGroupToRaid(APlayerState* TargetGroupLeaderPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_AcceptRaidInvite();

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_DeclineRaidInvite();

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_LeaveRaid(); // removes own group from the raid

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_KickMemberFromRaid(APlayerState* TargetPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_TransferRaidLeadership(APlayerState* NewRaidLeaderPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_PromoteToRaidAssistant(APlayerState* TargetPS);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRPC_DemoteRaidAssistant(APlayerState* TargetPS);

    // ── IGroupProvider forwarding (call from APlayerState::IGroupProvider impl) ──

    int32  GroupProvider_GetGroupSize()   const;
    bool   GroupProvider_IsGroupLeader()  const;
    void   GroupProvider_GetGroupMembers(TArray<APlayerState*>& Out) const;
    AActor* GroupProvider_GetGroupActor() const;
    AActor* GroupProvider_GetRaidActor()  const;
    void   GroupProvider_GetRaidMembers(TArray<APlayerState*>& Out) const;

protected:
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    // Cached subsystem pointer — set on first use, null-checked.
    UPROPERTY()
    TWeakObjectPtr<UGroupSubsystem> CachedSubsystem;

    UGroupSubsystem* GetSubsystem() const;
};
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

## RPC Implementations (representative subset)

### ServerRPC_InvitePlayer

```cpp
bool UGroupMemberComponent::ServerRPC_InvitePlayer_Validate(APlayerState* TargetPS)
{
    // Basic null and same-player check — full validation in the subsystem.
    return TargetPS != nullptr && TargetPS != GetOwner();
}

void UGroupMemberComponent::ServerRPC_InvitePlayer_Implementation(APlayerState* TargetPS)
{
    APlayerState* SelfPS = GetOwner<APlayerState>();
    GetSubsystem()->CreateInvite(SelfPS, TargetPS);
    // UGroupSubsystem::CreateInvite validates leader status and group capacity.
    // It fires GameCoreEvent.Group.InviteReceived on success.
}
```

### ServerRPC_AcceptInvite

```cpp
bool UGroupMemberComponent::ServerRPC_AcceptInvite_Validate() { return true; }

void UGroupMemberComponent::ServerRPC_AcceptInvite_Implementation()
{
    APlayerState* SelfPS = GetOwner<APlayerState>();
    GetSubsystem()->AcceptInvite(SelfPS);
}
```

### ServerRPC_LeaveGroup

```cpp
bool UGroupMemberComponent::ServerRPC_LeaveGroup_Validate() { return true; }

void UGroupMemberComponent::ServerRPC_LeaveGroup_Implementation()
{
    APlayerState* SelfPS = GetOwner<APlayerState>();
    AGroupActor* Group = GetSubsystem()->FindGroupForPlayer(SelfPS);
    if (!Group) return;

    // If in a raid and this player is a group leader, remove the group from the raid.
    ARaidActor* Raid = GetSubsystem()->FindRaidForPlayer(SelfPS);
    if (Raid && Group->GetGroupComponent()->GetLeader() == SelfPS)
        Raid->GetRaidComponent()->RemoveGroup(Group);

    Group->GetGroupComponent()->RemoveMember(SelfPS);
    // If group disbands (drops to 1), UGroupSubsystem::DisbandGroup is called automatically.
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
    APlayerState* SelfPS = GetOwner<APlayerState>();
    AGroupActor* Group = GetSubsystem()->FindGroupForPlayer(SelfPS);
    if (!Group) return;
    Group->GetGroupComponent()->KickMember(SelfPS, TargetPS);
}
```

---

## IGroupProvider Forwarding

These methods are called by `APlayerState` when it implements `IGroupProvider`. They read from the replicated `GroupView` on the client, and from the live `UGroupComponent` on the server.

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
    // Read from replicated view on client.
    APlayerState* Self = GetOwner<APlayerState>();
    for (const FGroupMemberEntry& E : GroupView.Members)
        if (E.MemberPlayerState == Self) return E.bIsGroupLeader;
    return false;
}

void UGroupMemberComponent::GroupProvider_GetGroupMembers(
    TArray<APlayerState*>& Out) const
{
    if (GetOwnerRole() == ROLE_Authority)
    {
        AGroupActor* Group = GetSubsystem()->FindGroupForPlayer(GetOwner<APlayerState>());
        if (Group) { Group->GetGroupComponent()->GetAllMembers(Out); return; }
    }
    // Client fallback: read from replicated view.
    for (const FGroupMemberEntry& E : GroupView.Members)
        if (E.MemberPlayerState) Out.Add(E.MemberPlayerState);
}

AActor* UGroupMemberComponent::GroupProvider_GetGroupActor() const
{
    // Only meaningful server-side; clients return null.
    if (GetOwnerRole() != ROLE_Authority) return nullptr;
    return GetSubsystem()->FindGroupForPlayer(GetOwner<APlayerState>());
}
```

---

## Wiring IGroupProvider on APlayerState

The game module implements `IGroupProvider` on `APlayerState` by forwarding to `UGroupMemberComponent`:

```cpp
// AMyPlayerState.h
class AMyPlayerState : public APlayerState, public IGroupProvider
{
    GENERATED_BODY()
public:
    virtual int32   GetGroupSize()                              const override;
    virtual bool    IsGroupLeader(const APlayerState* PS)       const override;
    virtual void    GetGroupMembers(TArray<APlayerState*>& Out) const override;
    virtual AActor* GetGroupActor()                             const override;
    virtual AActor* GetRaidActor()                              const override;
    virtual void    GetRaidMembers(TArray<APlayerState*>& Out)  const override;

    UPROPERTY()
    TObjectPtr<UGroupMemberComponent> GroupMemberComponent;
};

// AMyPlayerState.cpp
int32 AMyPlayerState::GetGroupSize() const
    { return GroupMemberComponent->GroupProvider_GetGroupSize(); }
bool AMyPlayerState::IsGroupLeader(const APlayerState* PS) const
    { return PS == this && GroupMemberComponent->GroupProvider_IsGroupLeader(); }
void AMyPlayerState::GetGroupMembers(TArray<APlayerState*>& Out) const
    { GroupMemberComponent->GroupProvider_GetGroupMembers(Out); }
AActor* AMyPlayerState::GetGroupActor() const
    { return GroupMemberComponent->GroupProvider_GetGroupActor(); }
AActor* AMyPlayerState::GetRaidActor() const
    { return GroupMemberComponent->GroupProvider_GetRaidActor(); }
void AMyPlayerState::GetRaidMembers(TArray<APlayerState*>& Out) const
    { GroupMemberComponent->GroupProvider_GetRaidMembers(Out); }
```
