// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/UnrealNetwork.h"
#include "FactionTypes.h"
#include "FactionComponent.generated.h"

/**
 * UFactionComponent
 *
 * Attach to any actor (player or NPC) that participates in faction logic.
 *
 * Mutation methods (JoinFaction, LeaveFaction, SetRank) are server-only.
 * Query methods (GetRelationshipTo, IsMemberOf, GetFactionTags) are safe on server and client.
 *
 * Memberships replicate delta-compressed via FFastArraySerializer.
 * LocalOverrides replicates in full (expected 0–3 entries max).
 * FallbackRelationship replicates in full.
 */
UCLASS(ClassGroup=(GameCore),
    meta=(BlueprintSpawnableComponent, DisplayName="Faction Component"))
class GAMECORE_API UFactionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFactionComponent();

    // ── Config ────────────────────────────────────────────────────────────

    // Used when this component has no primary memberships.
    // Resolution when both actors have no primaries: FMath::Min(Source.Fallback, Target.Fallback).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Factions")
    EFactionRelationship FallbackRelationship = EFactionRelationship::Neutral;

    // ── Memberships ───────────────────────────────────────────────────────

    // Replicated via FFastArraySerializer — delta-compressed per cycle.
    // For NPCs: populate in Blueprint defaults.
    // For players: populated at runtime by game module (loaded from save data).
    UPROPERTY(Replicated)
    FFactionMembershipArray Memberships;

    // Per-entity overrides checked before the global subsystem cache.
    // Use sparingly: bounty hunters, story NPCs with special standing.
    // Replicated in full. Expected to be 0–3 entries.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Factions")
    TArray<FFactionRelationshipOverride> LocalOverrides;

    // ── Delegates ─────────────────────────────────────────────────────────

    // Broadcast locally (server and owning client) after any join or leave.
    // For game module wiring — external systems should use the event bus.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
        FOnFactionMembershipChanged,
        FGameplayTag, FactionTag,
        bool, bJoined);
    UPROPERTY(BlueprintAssignable)
    FOnFactionMembershipChanged OnMembershipChanged;

    // Broadcast after SetRank succeeds. For game module reputation wiring.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
        FOnFactionRankChanged,
        FGameplayTag, FactionTag,
        FGameplayTag, NewRankTag);
    UPROPERTY(BlueprintAssignable)
    FOnFactionRankChanged OnRankChanged;

    // ── Mutation — Server Only ────────────────────────────────────────────

    // Evaluates JoinRequirements from UFactionDefinition, adds the membership,
    // broadcasts event bus + OnMembershipChanged.
    // Returns false if requirements fail; OutFailureReason is populated.
    // Idempotent: returns true if already a member.
    // Authority guard: logs error and returns false on client.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool JoinFaction(FGameplayTag FactionTag, FText& OutFailureReason,
        bool bPrimary = true);

    // Removes membership. Broadcasts event bus + OnMembershipChanged.
    // No-op and returns false if not a member.
    // Authority: server only.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool LeaveFaction(FGameplayTag FactionTag);

    // Sets the rank tag for an existing membership.
    // RankTag must be in UFactionDefinition::RankTags, or empty.
    // Validated via ensure() in non-shipping builds.
    // Authority: server only.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool SetRank(FGameplayTag FactionTag, FGameplayTag RankTag);

    // ── Queries — Safe on Server and Client ───────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool IsMemberOf(FGameplayTag FactionTag) const;

    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool GetMembership(FGameplayTag FactionTag,
        FFactionMembership& OutMembership) const;

    // Resolves the worst relationship toward another component.
    // Delegates entirely to UFactionSubsystem::GetActorRelationship.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    EFactionRelationship GetRelationshipTo(
        const UFactionComponent* Other) const;

    // Fills OutTags with faction tags on this component.
    // bPrimaryOnly = true: only primary memberships.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    void GetFactionTags(FGameplayTagContainer& OutTags,
        bool bPrimaryOnly = false) const;

    // ── UActorComponent ───────────────────────────────────────────────────

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
