// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FactionTypes.h"
#include "FactionSubsystem.generated.h"

class UFactionDefinition;
class UFactionRelationshipTable;
class UFactionComponent;

/**
 * UFactionSubsystem
 *
 * UWorldSubsystem. Loads UFactionRelationshipTable and builds RelationshipCache at world start.
 * Provides O(1) relationship queries used by UFactionComponent, AI, and interaction gating.
 *
 * Thread safety: NOT thread-safe. All methods must be called on the game thread.
 * RelationshipCache is written once during BuildCache() and read-only thereafter.
 */
UCLASS()
class GAMECORE_API UFactionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    // ── UWorldSubsystem ───────────────────────────────────────────────────

    virtual void OnWorldBeginPlay(UWorld& World) override;

    // ── Relationship Queries (callable on server and client) ──────────────

    // Resolves the relationship between two specific faction tags.
    // Returns the cached explicit value, or resolves via DefaultRelationship
    // FMath::Min on a cache miss. Returns Neutral if either tag is unregistered.
    // Self-relationship (FactionA == FactionB) always returns Ally.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    EFactionRelationship GetRelationship(
        FGameplayTag FactionA, FGameplayTag FactionB) const;

    // Resolves the worst (least-friendly) relationship across all primary faction
    // pairs between two components. Checks LocalOverrides from both components
    // before the cache. Returns FallbackRelationship when either component has
    // no primary memberships.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    EFactionRelationship GetActorRelationship(
        const UFactionComponent* Source,
        const UFactionComponent* Target) const;

    // Returns all registered faction tags that are Hostile toward any primary
    // faction on Source. Excludes factions Source is already a member of.
    // Used by AI perception and combat targeting systems.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    void GetHostileFactions(
        const UFactionComponent* Source,
        TArray<FGameplayTag>& OutHostile) const;

    // Returns the UFactionDefinition for a given faction tag, or null if not registered.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    const UFactionDefinition* GetDefinition(FGameplayTag FactionTag) const;

    // Returns the faction tag whose ReputationProgression matches the given
    // progression tag. Returns invalid tag if not found.
    // Used by game module reputation wiring — see Architecture.md wiring guide.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    FGameplayTag FindFactionByReputationProgression(
        FGameplayTag ProgressionTag) const;

    // ── Dev Validation ────────────────────────────────────────────────────

#if !UE_BUILD_SHIPPING
    // Logs warnings/errors for:
    //   - Factions referenced in ExplicitRelationships not present in Factions array
    //   - Faction tags with no valid UFactionDefinition asset
    //   - Duplicate pair entries in ExplicitRelationships
    void ValidateTable() const;
#endif

private:

    // Populated once in OnWorldBeginPlay. Never mutated at runtime.
    TMap<FFactionSortedPair, EFactionRelationship> RelationshipCache;

    // Faction tag → loaded UFactionDefinition*.
    UPROPERTY()
    TMap<FGameplayTag, TObjectPtr<UFactionDefinition>> DefinitionMap;

    // Reputation progression tag → faction tag. Used by FindFactionByReputationProgression.
    TMap<FGameplayTag, FGameplayTag> ReputationProgressionMap;

    UPROPERTY()
    TObjectPtr<UFactionRelationshipTable> Table;

    void BuildCache();

    // Returns FMath::Min of the two factions' DefaultRelationship uint8 values.
    // Returns Neutral if either faction is not in DefinitionMap.
    EFactionRelationship ResolveDefault(
        FGameplayTag FactionA, FGameplayTag FactionB) const;

    // Checks LocalOverrides on Source and Target for any pair covering (FactionA, FactionB).
    // Returns true and sets OutRelationship if a local override is found.
    bool CheckLocalOverrides(
        const UFactionComponent* Source,
        const UFactionComponent* Target,
        FGameplayTag FactionA,
        FGameplayTag FactionB,
        EFactionRelationship& OutRelationship) const;
};
