// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GroupTypes.h"
#include "GroupSubsystem.generated.h"

class AGroupActor;
class ARaidActor;
class UGroupConfigDataAsset;

/**
 * UGroupSubsystem
 *
 * UWorldSubsystem. Server-only. Central registry and lifecycle manager for all
 * active groups and raids. Spawns and destroys AGroupActor and ARaidActor.
 * Manages pending invites, disconnect grace timers, and reconnect matching.
 *
 * AGameMode must call OnPlayerDisconnected and OnPlayerReconnected explicitly.
 */
UCLASS()
class GAMECORE_API UGroupSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    // ── Lifecycle hooks — call from AGameMode ───────────────────────────────

    // Call from AGameMode::NotifyPlayerDisconnected.
    void OnPlayerDisconnected(APlayerState* PS);

    // Call from AGameMode::PostLogin when a player logs in.
    // No-op if the player is not in a grace window.
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
    bool CreateInvite(APlayerState* InviterPS, APlayerState* TargetPS);

    // Creates a raid invite targeting a group leader.
    bool CreateRaidInvite(APlayerState* RaidLeaderPS, APlayerState* TargetGroupLeaderPS);

    // Accepts the pending invite for TargetPS.
    bool AcceptInvite(APlayerState* TargetPS);

    // Declines or cancels the pending invite for TargetPS.
    void DeclineInvite(APlayerState* TargetPS);

    // ── Raid management ─────────────────────────────────────────────────────

    // Creates a new ARaidActor with InitiatingGroup as the first group.
    ARaidActor* CreateRaid(AGroupActor* InitiatingGroup);

    // Removes a group from the raid. Pass nullptr Group to force-dissolve.
    // Destroys ARaidActor if only one group remains after removal.
    void RemoveGroupFromRaid(AGroupActor* Group, ARaidActor* Raid);

    // ── Config accessor ──────────────────────────────────────────────────────

    const UGroupConfigDataAsset* GetConfig() const;

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

    // Pending invites. Key = UniqueNetId string of the target.
    TMap<FString, FPendingGroupInvite> PendingInvites;

    // Disconnect grace entries. Key = UniqueNetId string.
    TMap<FString, FDisconnectGraceEntry> GraceEntries;

    // Fast lookup: UniqueNetId string → AGroupActor.
    TMap<FString, TWeakObjectPtr<AGroupActor>> PlayerToGroup;

    // Repeating timer for invite expiry polling (fires every 1 s).
    FTimerHandle InviteExpiryTimerHandle;

    void TickInviteExpiry();
    void OnGraceExpired(FString UniqueNetIdString);
};
