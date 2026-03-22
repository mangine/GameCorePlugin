// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GroupTypes.h"
#include "RaidComponent.generated.h"

class AGroupActor;
class ARaidActor;
class UGroupConfigDataAsset;

/**
 * URaidComponent
 *
 * Server-only. Owned by ARaidActor.
 * Holds the group list, raid leader, and raid assistant authority model.
 * Responsible for recomputing raid roles, dissolving the raid, and providing
 * cross-group FGroupMemberEntry data for BuildViewForMember.
 */
UCLASS()
class GAMECORE_API URaidComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Group management ──────────────────────────────────────────────────

    // Adds a group to the raid. Returns false if raid is at MaxGroupsPerRaid.
    bool AddGroup(AGroupActor* Group);

    // Removes a group. Transfers raid leadership if needed.
    // Dissolves raid if only one group remains.
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
    // KI-1 fix: calls GC->GetAllMembers(Members) before iterating, not empty literal.
    void AssignRaidRoles();

    // Pushes updated views to all groups.
    void BroadcastRaidViewToAllGroups();

    // Transfers raid leadership to the first valid group leader found.
    void TransferRaidLeadershipToNextGroup();
};
