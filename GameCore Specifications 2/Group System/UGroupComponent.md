# UGroupComponent

**File:** `Group/GroupComponent.h` / `GroupComponent.cpp`
**Authority:** Server only
**Owner:** `AGroupActor`

Holds all member state and mutation logic for a single group. Drives the shared-data heartbeat tick. Responsible for building and pushing `FGroupMemberView` to each member's `UGroupMemberComponent`.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UGroupComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Membership mutation (server only) ──────────────────────────────────

    // Invite-flow path. Validates max size. Returns false if group is full or PS already a member.
    bool AddMember(APlayerState* PS);

    // Matchmaking / programmatic path. Same validation as AddMember.
    // Must be called server-side only — guarded by check(!IsRunningClientOnly()).
    bool ForceAddMember(APlayerState* PS);

    // Removes a member by pointer. Triggers leader transfer if needed.
    // Calls UGroupSubsystem::DisbandGroup if count drops to 1.
    void RemoveMember(APlayerState* PS);

    // Removes a member by UniqueNetId string (used when PS is no longer valid after grace expiry).
    void RemoveMemberByNetId(const FString& NetId);

    // ── Disconnect / reconnect ─────────────────────────────────────────────

    // Called by UGroupSubsystem::OnPlayerDisconnected. Marks slot as disconnected.
    void MarkMemberDisconnected(APlayerState* PS);

    // Called by UGroupSubsystem::OnPlayerReconnected.
    // Replaces the TObjectPtr in the existing slot at SlotIndex with the new PS.
    void RestoreMember(APlayerState* PS, uint8 SlotIndex);

    // ── Leadership ─────────────────────────────────────────────────────────

    APlayerState* GetLeader() const;

    // Promotes NewLeader. Current leader loses the role.
    // Valid only if called by the current leader (enforced at the RPC layer).
    void TransferLeadership(APlayerState* NewLeader);

    // Promotes the connected member with the lowest SlotIndex.
    // Called automatically on leader disconnect.
    void TransferLeadershipToNextSlot();

    // ── Kick ──────────────────────────────────────────────────────────────

    // Removes TargetPS by the group leader's authority.
    // Returns false if KickerPS is not the group leader, or TargetPS == KickerPS.
    bool KickMember(APlayerState* KickerPS, APlayerState* TargetPS);

    // ── Raid role ─────────────────────────────────────────────────────────

    // Set by URaidComponent::AssignRaidRoles after raid composition changes.
    void SetRaidRoleForMember(APlayerState* PS, EGroupRaidRole Role);
    EGroupRaidRole GetRaidRole(const APlayerState* PS) const;

    // Returns the RaidSharedData for a member (used by URaidComponent when building cross-group entries).
    UMemberSharedData* GetRaidSharedData(const APlayerState* PS) const;

    // ── Queries ────────────────────────────────────────────────────────────

    int32 GetMemberCount() const;        // total slots including disconnected-grace
    int32 GetActiveMemberCount() const;  // excludes disconnected-grace slots
    uint8 GetSlotIndex(const APlayerState* PS) const;
    bool  IsMember(const APlayerState* PS) const;
    void  GetAllMembers(TArray<APlayerState*>& Out) const; // skips disconnected slots

    // ── View ──────────────────────────────────────────────────────────────

    // Rebuilds and pushes FGroupMemberView to all non-disconnected members.
    void PushViewToAllMembers();

    // Builds FGroupMemberView for a specific member's perspective.
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

    uint8 AllocateSlotIndex() const;                    // returns lowest free slot index
    void  AllocateSharedData(FGroupMemberEntry& Entry) const;
    void  GatherSharedData(FGroupMemberEntry& Entry);   // calls GatherFromPlayerState
    void  GatherAllSharedData();                        // iterates Members
};
```

---

## BeginPlay

```cpp
void UGroupComponent::BeginPlay()
{
    Super::BeginPlay();
    // Cache config once. GetWorld()->GetSubsystem is safe in BeginPlay.
    UGroupSubsystem* GS = GetWorld()->GetSubsystem<UGroupSubsystem>();
    check(GS);
    Config = GS->GetConfig();
    check(Config);
    SetComponentTickEnabled(Config->GroupSharedDataClass != nullptr ||
                            Config->RaidSharedDataClass  != nullptr);
}
```

> Tick is only enabled when at least one shared data class is configured. This avoids a wasted per-frame tick for projects that don't use shared data.

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

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::MemberJoined,
        FInstancedStruct::Make(FGroupMemberChangedMessage{ PS, GetOwner<AGroupActor>() }),
        EGameCoreEventScope::ServerOnly);
    return true;
}

bool UGroupComponent::ForceAddMember(APlayerState* PS)
{
    check(!IsRunningClientOnly()); // server-side guard
    return AddMember(PS);
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
    {
        TransferLeadershipToNextSlot();
        UGameCoreEventBus::Get(this)->Broadcast(
            GameCoreGroupTags::LeaderChanged,
            FInstancedStruct::Make(FGroupLeaderChangedMessage{ GetOwner<AGroupActor>(), GetLeader(), PS }),
            EGameCoreEventScope::ServerOnly);
    }

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::MemberLeft,
        FInstancedStruct::Make(FGroupMemberChangedMessage{ PS, GetOwner<AGroupActor>() }),
        EGameCoreEventScope::ServerOnly);

    // Disband if only one (or zero) members remain.
    if (Members.Num() <= 1)
    {
        GetWorld()->GetSubsystem<UGroupSubsystem>()->DisbandGroup(GetOwner<AGroupActor>());
        return; // actor is being destroyed — do not touch Members after this
    }

    PushViewToAllMembers();
}
```

---

## RemoveMemberByNetId

```cpp
void UGroupComponent::RemoveMemberByNetId(const FString& NetId)
{
    int32 Idx = Members.IndexOfByPredicate(
        [&NetId](const FGroupMemberEntry& E){ return E.UniqueNetIdString == NetId; });
    if (Idx == INDEX_NONE) return;

    // Build a temporary PS pointer (may be null for a disconnected slot).
    APlayerState* PS = Members[Idx].MemberPlayerState.Get();
    bool bWasLeader  = Members[Idx].bIsGroupLeader;
    Members.RemoveAt(Idx);

    if (bWasLeader && Members.Num() > 0)
        TransferLeadershipToNextSlot();

    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::MemberLeft,
        FInstancedStruct::Make(FGroupMemberChangedMessage{ PS, GetOwner<AGroupActor>() }),
        EGameCoreEventScope::ServerOnly);

    if (Members.Num() <= 1)
    {
        GetWorld()->GetSubsystem<UGroupSubsystem>()->DisbandGroup(GetOwner<AGroupActor>());
        return;
    }

    PushViewToAllMembers();
}
```

---

## KickMember

```cpp
bool UGroupComponent::KickMember(APlayerState* KickerPS, APlayerState* TargetPS)
{
    if (GetLeader() != KickerPS) return false;
    if (!IsMember(TargetPS))     return false;
    if (KickerPS == TargetPS)    return false; // leader cannot kick themselves; use Leave

    RemoveMember(TargetPS);

    // Notify kicked player.
    UGameCoreEventBus::Get(this)->Broadcast(
        GameCoreGroupTags::MemberKicked,
        FInstancedStruct::Make(FGroupMemberChangedMessage{ TargetPS, GetOwner<AGroupActor>() }),
        EGameCoreEventScope::ServerOnly);
    return true;
}
```

---

## TransferLeadershipToNextSlot

```cpp
void UGroupComponent::TransferLeadershipToNextSlot()
{
    for (FGroupMemberEntry& E : Members)
        E.bIsGroupLeader = false;

    FGroupMemberEntry* Best = nullptr;
    for (FGroupMemberEntry& E : Members)
    {
        if (E.bDisconnected) continue;
        if (!Best || E.SlotIndex < Best->SlotIndex)
            Best = &E;
    }
    if (Best)
        Best->bIsGroupLeader = true;
    // Caller is responsible for PushViewToAllMembers after this returns.
}
```

---

## TickComponent — Shared Data Heartbeat

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

        MemberComp->SetGroupView(BuildViewForMember(E.MemberPlayerState.Get()));
    }
}
```

---

## BuildViewForMember

Builds the `FGroupMemberView` for the given player's perspective.

```cpp
FGroupMemberView UGroupComponent::BuildViewForMember(const APlayerState* ForPS) const
{
    FGroupMemberView View;
    View.Members = Members; // copy own group entries (GroupSharedData populated)

    ARaidActor* Raid = GetOwner<AGroupActor>()->GetOwningRaid();
    if (Raid)
    {
        View.bIsInRaid = true;
        View.GroupIndexInRaid = Raid->GetRaidComponent()->GetGroupIndex(GetOwner<AGroupActor>());
        Raid->GetRaidComponent()->PopulateRaidMemberEntries(
            GetOwner<AGroupActor>(), View.RaidMembers);
    }
    return View;
}
```

**Note:** `URaidComponent::GetGroupIndex(AGroupActor*)` returns the group's 0-based index in the `Groups` array.

---

## AllocateSharedData

```cpp
void UGroupComponent::AllocateSharedData(FGroupMemberEntry& Entry) const
{
    AActor* Owner = GetOwner();
    if (Config->GroupSharedDataClass)
        Entry.GroupSharedData = NewObject<UMemberSharedData>(Owner, Config->GroupSharedDataClass);
    if (Config->RaidSharedDataClass)
        Entry.RaidSharedData = NewObject<UMemberSharedData>(Owner, Config->RaidSharedDataClass);
}
```

---

## AllocateSlotIndex

```cpp
uint8 UGroupComponent::AllocateSlotIndex() const
{
    TSet<uint8> UsedSlots;
    for (const FGroupMemberEntry& E : Members)
        UsedSlots.Add(E.SlotIndex);
    for (uint8 i = 0; i < 255; ++i)
        if (!UsedSlots.Contains(i)) return i;
    return 0; // should never reach here given MaxGroupSize constraint
}
```
