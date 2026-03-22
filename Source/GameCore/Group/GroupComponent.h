// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GroupTypes.h"
#include "GroupComponent.generated.h"

class UGroupSubsystem;
class UGroupConfigDataAsset;
class UGroupMemberComponent;

/**
 * UGroupComponent
 *
 * Server-only. Owned by AGroupActor.
 * Holds all member state and mutation logic for a single group.
 * Drives the shared-data heartbeat tick.
 * Builds and pushes FGroupMemberView to each member's UGroupMemberComponent.
 */
UCLASS()
class GAMECORE_API UGroupComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Membership mutation (server only) ──────────────────────────────────

    // Invite-flow path. Validates max size. Returns false if group is full or PS already a member.
    bool AddMember(APlayerState* PS);

    // Matchmaking / programmatic path. Same validation as AddMember.
    // Guarded by check(!IsRunningClientOnly()).
    bool ForceAddMember(APlayerState* PS);

    // Removes a member by pointer. Triggers leader transfer if needed.
    // Calls UGroupSubsystem::DisbandGroup if count drops to 1.
    void RemoveMember(APlayerState* PS);

    // Removes a member by UniqueNetId string (used when PS is no longer valid after grace expiry).
    void RemoveMemberByNetId(const FString& NetId);

    // ── Disconnect / reconnect ─────────────────────────────────────────────

    void MarkMemberDisconnected(APlayerState* PS);
    void RestoreMember(APlayerState* PS, uint8 SlotIndex);

    // ── Leadership ─────────────────────────────────────────────────────────

    APlayerState* GetLeader() const;
    void TransferLeadership(APlayerState* NewLeader);
    void TransferLeadershipToNextSlot();

    // ── Kick ──────────────────────────────────────────────────────────────

    bool KickMember(APlayerState* KickerPS, APlayerState* TargetPS);

    // ── Raid role ─────────────────────────────────────────────────────────

    void SetRaidRoleForMember(APlayerState* PS, EGroupRaidRole Role);
    EGroupRaidRole GetRaidRole(const APlayerState* PS) const;

    // Returns the RaidSharedData for a member (used by URaidComponent for cross-group entries).
    UMemberSharedData* GetRaidSharedData(const APlayerState* PS) const;

    // ── Queries ────────────────────────────────────────────────────────────

    int32 GetMemberCount() const;        // total slots including disconnected-grace
    int32 GetActiveMemberCount() const;  // excludes disconnected-grace slots
    uint8 GetSlotIndex(const APlayerState* PS) const;
    bool  IsMember(const APlayerState* PS) const;
    void  GetAllMembers(TArray<APlayerState*>& Out) const; // skips disconnected slots

    // ── View ──────────────────────────────────────────────────────────────

    void PushViewToAllMembers();
    FGroupMemberView BuildViewForMember(const APlayerState* ForPS) const;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY()
    TArray<FGroupMemberEntry> Members; // server-side authoritative list

    const UGroupConfigDataAsset* Config = nullptr;
    float TimeSinceLastHeartbeat = 0.f;

    uint8 AllocateSlotIndex() const;
    void  AllocateSharedData(FGroupMemberEntry& Entry) const;
    void  GatherSharedData(FGroupMemberEntry& Entry);
    void  GatherAllSharedData();
};
