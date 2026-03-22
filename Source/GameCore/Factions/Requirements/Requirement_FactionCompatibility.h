// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Requirements/Requirement.h"
#include "Requirements/RequirementContext.h"
#include "Factions/FactionTypes.h"
#include "GameplayTagContainer.h"
#include "Requirement_FactionCompatibility.generated.h"

// ─── Context struct for faction requirement evaluation ────────────────────────

/**
 * FFactionRequirementContext
 *
 * Packed into FRequirementContext::Data when evaluating faction join requirements.
 * Contains the actor joining the faction and the world pointer needed to access
 * UFactionSubsystem.
 *
 * Use FRequirementContext::Make<FFactionRequirementContext>() at call site.
 */
USTRUCT()
struct GAMECORE_API FFactionRequirementContext
{
    GENERATED_BODY()

    // The actor attempting to join the faction.
    UPROPERTY()
    TObjectPtr<AActor> Instigator = nullptr;

    // World pointer for subsystem access.
    UPROPERTY()
    TObjectPtr<UWorld> World = nullptr;
};

// =============================================================================
// URequirement_FactionCompatibility
// =============================================================================

/**
 * URequirement_FactionCompatibility
 *
 * Blocks JoinFaction() if the joining actor has any primary faction membership
 * whose relationship toward TargetFactionTag is below MinimumAllowedRelationship.
 *
 * Server-only. Evaluates via UFactionSubsystem::GetRelationship.
 * Must not appear in ClientOnly or ClientValidated requirement sets.
 */
UCLASS(EditInlineNew, CollapseCategories,
    meta=(DisplayName="Faction Compatibility"))
class GAMECORE_API URequirement_FactionCompatibility : public URequirement
{
    GENERATED_BODY()
public:

    // The faction being joined. Relationship toward this faction is checked
    // for all existing primary memberships of the joining actor.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Requirement")
    FGameplayTag TargetFactionTag;

    // Minimum relationship the joining actor's existing primary factions must
    // have toward TargetFactionTag.
    //
    // Unfriendly (default): blocks join only if a current faction is Hostile.
    // Neutral:              blocks join if any conflict exists.
    // Ally:                 requires existing ally status (prestige unlock).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Requirement")
    EFactionRelationship MinimumAllowedRelationship =
        EFactionRelationship::Unfriendly;

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override;

    // Event-driven watcher: invalidate when any faction membership changes.
    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override;

#if WITH_EDITOR
    virtual FString GetDescription() const override;
#endif
};
