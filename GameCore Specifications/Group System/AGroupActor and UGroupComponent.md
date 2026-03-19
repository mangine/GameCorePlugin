# AGroupActor and UGroupComponent

**Sub-page of:** [Group System](../Group%20System.md)
**Files:** `Group/GroupActor.h / .cpp`, `Group/GroupComponent.h / .cpp`
**Authority:** Server only

`AGroupActor` is the server-side owner of one group. It implements `IGroupProvider` so that existing GameCore systems (Quest, Requirement) can consume group data without a hard dependency on the Group module. `UGroupComponent` holds all member state and mutation logic.

---

## AGroupActor

```cpp
ACLASS(NotBlueprintable)
class GAMECORE_API AGroupActor : public AActor, public IGroupProvider
{
    GENERATED_BODY()
public:
    AGroupActor();

    UGroupComponent* GetGroupComponent() const { return GroupComponent; }

    // ── IGroupProvider ─────────────────────────────────────────────────────
    virtual int32  GetGroupSize()                                   const override;
    virtual bool   IsGroupLeader(const APlayerState* PS)            const override;
    virtual void   GetGroupMembers(TArray<APlayerState*>& Out)      const override;
    virtual AActor* GetGroupActor()                                 const override { return const_cast<AGroupActor*>(this); }
    virtual AActor* GetRaidActor()                                  const override;
    virtual void   GetRaidMembers(TArray<APlayerState*>& Out)       const override;

private:
    UPROPERTY()
    TObjectPtr<UGroupComponent> GroupComponent;

    // Backref to the raid this group belongs to, if any. Null if not in a raid.
    UPROPERTY()
    TWeakObjectPtr<ARaidActor> OwningRaid;

    friend class URaidComponent; // allowed to set OwningRaid
};
```

**Constructor:**
```cpp
AGroupActor::AGroupActor()
{
    // Server-only actor. Never replicate.
    bReplicates = false;
    bNetLoadOnClient = false;
    PrimaryActorTick.bCanEverTick = false;

    GroupComponent = CreateDefaultSubobject<UGroupComponent>(TEXT("GroupComponent"));
}
```

---

## UGroupComponent

```cpp
UCLASS()
class GAMECORE_API UGroupComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Membership mutation (server only) ─────────────────────────────────

    // Invite-flow path. Validates max size. Returns false if group is full.
    bool AddMember(APlayerState* PS);

    // Matchmaking / programmatic path. Validates max size. Bypasses invite logic.
    // Call server-side only — guarded by check(!IsRunningClientOnly()).
    bool ForceAddMember(APlayerState* PS);

    // Removes a member. Triggers leader transfer if needed. Calls UGroupSubsystem
    // to disband if count drops to 1 (solo player has no group).
    void RemoveMember(APlayerState* PS);

    // Called by UGroupSubsystem::OnPlayerDisconnected.
    void MarkMemberDisconnected(APlayerState* PS);

    // Called by UGroupSubsystem::OnPlayerReconnected.
    // Restores PS into the slot at SlotIndex.
    void RestoreMember(APlayerState* PS, uint8 SlotIndex);

    // ── Leadership ────────────────────────────────────────────────────────

    APlayerState* GetLeader() const;

    // Promotes NewLeader. Current leader loses the role.
    // Only valid if called by the current leader (enforced by the RPC layer).
    void TransferLeadership(APlayerState* NewLeader);

    // Promotes the member with the next lowest SlotIndex after the current leader.
    // Called automatically on leader disconnect.
    void TransferLeadershipToNextSlot();

    // ── Kick ──────────────────────────────────────────────────────────────

    // Removes a member by the group leader's authority.
    // Returns false if KickerPS is not the group leader.
    bool KickMember(APlayerState* KickerPS, APlayerState* TargetPS);

    // ── Queries ───────────────────────────────────────────────────────────

    int32 GetMemberCount() const;
    int32 GetActiveMemberCount() const; // excludes disconnected-grace slots
    uint8 GetSlotIndex(const APlayerState* PS) const;
    bool  IsMember(const APlayerState* PS) const;
    void  GetAllMembers(TArray<APlayerState*>& Out) const; // skips disconnected slots

    // ── View ──────────────────────────────────────────────────────────────

    // Rebuilds and pushes FGroupMemberView to all non-disconnected members.
    void PushViewToAllMembers();

    // Builds the FGroupMemberView for a specific member (their own perspective).
    FGroupMemberView BuildViewForMember(const APlayerState* ForPS) const;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY()
    TArray<FGroupMemberEntry> Members; // server-side authoritative list

    // Cached config pointer, set in BeginPlay.
    const UGroupConfigDataAsset* Config = nullptr;

    // Accumulator for shared data heartbeat.
    float TimeSinceLastHeartbeat = 0.f;

    uint8 AllocateSlotIndex() const; // returns lowest free slot index
    void  AllocateSharedData(FGroupMemberEntry& Entry) const;
    void  GatherSharedData(FGroupMemberEntry& Entry);
    void  GatherAllSharedData();
};
```

---

## AddMember / ForceAddMember

```cpp
bool UGroupComponent::AddMember(APlayerState* PS)
{
    if (!PS) return false;
    if (Members.Num() >= Config->MaxGroupSize) return false;
    if (IsMember(PS)) return false;

    FGroupMemberEntry& Entry = Members.AddDefaulted_GetRef();
    Entry.MemberPlayerState = PS;
    Entry.UniqueNetIdString  = PS->GetUniqueId().ToString();
    Entry.SlotIndex          = AllocateSlotIndex();
    Entry.bIsGroupLeader     = (Members.Num() == 1); // first member is leader
    AllocateSharedData(Entry);

    PushViewToAllMembers();
    // Fire GMS event
    return true;
}

bool UGroupComponent::ForceAddMember(APlayerState* PS)
{
    check(!IsRunningClientOnly()); // server-side only
    return AddMember(PS); // same logic; invite validation is skipped at the callsite (UGroupSubsystem)
}
```

---

## RemoveMember

```cpp
void UGroupComponent::RemoveMember(APlayerState* PS)
{
    int32 Idx = Members.IndexOfByPredicate(
        [PS](const FGroupMemberEntry& E){ return E.MemberPlayerState == PS; });
    if (Idx == INDEX_NONE) return;

    bool bWasLeader = Members[Idx].bIsGroupLeader;
    Members.RemoveAt(Idx);

    if (bWasLeader && Members.Num() > 0)
        TransferLeadershipToNextSlot();

    PushViewToAllMembers();

    // Disband if only one member remains — a solo player has no group.
    if (Members.Num() <= 1)
    {
        UGroupSubsystem* GS = GetWorld()->GetSubsystem<UGroupSubsystem>();
        GS->DisbandGroup(GetOwner<AGroupActor>());
        return;
    }
    // Fire GMS event
}
```

---

## TransferLeadershipToNextSlot

```cpp
void UGroupComponent::TransferLeadershipToNextSlot()
{
    // Clear current leader flag.
    for (FGroupMemberEntry& E : Members)
        E.bIsGroupLeader = false;

    // Find the connected member with the lowest slot index.
    FGroupMemberEntry* Best = nullptr;
    for (FGroupMemberEntry& E : Members)
    {
        if (E.bDisconnected) continue;
        if (!Best || E.SlotIndex < Best->SlotIndex)
            Best = &E;
    }
    if (Best)
        Best->bIsGroupLeader = true;
    // PushViewToAllMembers called by the caller after this returns.
}
```

---

## Shared Data Tick

```cpp
void UGroupComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                     FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    TimeSinceLastHeartbeat += DeltaTime;
    bool bHeartbeat = TimeSinceLastHeartbeat >= Config->SharedDataHeartbeatInterval;

    bool bAnyDirty = bHeartbeat;
    if (!bHeartbeat)
    {
        for (const FGroupMemberEntry& E : Members)
        {
            if ((E.GroupSharedData && E.GroupSharedData->IsDirty()) ||
                (E.RaidSharedData  && E.RaidSharedData->IsDirty()))
            {
                bAnyDirty = true;
                break;
            }
        }
    }

    if (!bAnyDirty) return;

    if (bHeartbeat) TimeSinceLastHeartbeat = 0.f;

    GatherAllSharedData();
    PushViewToAllMembers();
}
```

---

## PushViewToAllMembers

```cpp
void UGroupComponent::PushViewToAllMembers()
{
    for (const FGroupMemberEntry& E : Members)
    {
        if (E.bDisconnected || !E.MemberPlayerState) continue;

        UGroupMemberComponent* MemberComp =
            E.MemberPlayerState->FindComponentByClass<UGroupMemberComponent>();
        if (!MemberComp) continue;

        MemberComp->SetGroupView(BuildViewForMember(E.MemberPlayerState));
    }
}
```

**BuildViewForMember** constructs a `FGroupMemberView` for the given player:
- `Members`: all `FGroupMemberEntry` items, each with `GroupSharedData` populated.
- `RaidMembers`: entries for members in other raid groups, each with `RaidSharedData` only (`GroupSharedData = null`).
- `bIsInRaid` and `GroupIndexInRaid` from `OwningRaid`.

---

## AllocateSharedData

```cpp
void UGroupComponent::AllocateSharedData(FGroupMemberEntry& Entry) const
{
    if (Config->GroupSharedDataClass)
        Entry.GroupSharedData = NewObject<UMemberSharedData>(
            GetOwner(), Config->GroupSharedDataClass);

    if (Config->RaidSharedDataClass)
        Entry.RaidSharedData = NewObject<UMemberSharedData>(
            GetOwner(), Config->RaidSharedDataClass);
}
```

---

## KickMember

```cpp
bool UGroupComponent::KickMember(APlayerState* KickerPS, APlayerState* TargetPS)
{
    if (GetLeader() != KickerPS) return false;
    if (!IsMember(TargetPS)) return false;
    if (KickerPS == TargetPS) return false; // leader cannot kick themselves; use Leave

    RemoveMember(TargetPS);
    // Notify target via GMS: GameCoreEvent.Group.Kicked
    return true;
}
```
