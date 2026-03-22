// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "GroupTypes.h"
#include "GroupMemberComponent.generated.h"

class UGroupSubsystem;
class AGroupActor;
class ARaidActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGroupViewChanged, const FGroupMemberView&, NewView);

/**
 * UGroupMemberComponent
 *
 * Lives on every APlayerState. Owns the replicated FGroupMemberView snapshot
 * and is the RPC entry point for all player-initiated group actions.
 *
 * Clients never call AGroupActor or URaidComponent directly — everything routes
 * through RPCs on this component.
 */
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UGroupMemberComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    // ── Client-readable state ──────────────────────────────────────────────

    // Current group/raid snapshot. Replicates COND_OwnerOnly.
    UPROPERTY(ReplicatedUsing=OnRep_GroupView, BlueprintReadOnly, Category="Group")
    FGroupMemberView GroupView;

    UFUNCTION()
    void OnRep_GroupView();

    const FGroupMemberView& GetGroupView() const { return GroupView; }

    // Fired on the client whenever GroupView is updated via replication.
    UPROPERTY(BlueprintAssignable, Category="Group")
    FOnGroupViewChanged OnGroupViewChanged;

    // ── Server-side write ──────────────────────────────────────────────────

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
