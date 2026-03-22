// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FactionTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include "FactionDefinition.generated.h"

class URequirement;
class ULevelProgressionDefinition;

/**
 * UFactionDefinition
 *
 * Static configuration asset for a single faction.
 * One asset per faction. Never modified at runtime.
 * Referenced by UFactionRelationshipTable and loaded into UFactionSubsystem::DefinitionMap at world start.
 */
UCLASS(BlueprintType)
class GAMECORE_API UFactionDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // ── Identity ─────────────────────────────────────────────────────────

    // Unique gameplay tag for this faction.
    // Convention: Faction.<Group> — e.g. Faction.Navy, Faction.Pirates.BlackSails
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FGameplayTag FactionTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FText DisplayName;

    // Optional flavour icon for UI. Loaded on demand — never auto-loaded by GameCore.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    TSoftObjectPtr<UTexture2D> Icon;

    // ── Relationships ─────────────────────────────────────────────────────

    // Fallback relationship used when no explicit pair entry exists in the table.
    // Resolution: FMath::Min(DefaultA, DefaultB) — least friendly always wins.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Relationship")
    EFactionRelationship DefaultRelationship = EFactionRelationship::Neutral;

    // ── Join Requirements ─────────────────────────────────────────────────

    // Evaluated server-side when JoinFaction() is called.
    // All entries must be synchronous — validated at BeginPlay in non-shipping builds.
    // Example: URequirement_FactionCompatibility, URequirement_MinLevel,
    //          URequirement_QuestCompleted.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Join")
    TArray<TObjectPtr<URequirement>> JoinRequirements;

    // ── Reputation (optional hook for game module wiring) ─────────────────

    // Soft reference to the Progression definition that tracks reputation XP
    // for this faction (e.g. DA_Progression_NavyReputation).
    // GameCore does NOT listen to progression events or map levels to ranks.
    // This field is documentation/config for game module wiring only.
    // See Usage.md — Reputation / Rank Wiring section.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reputation",
        meta = (ToolTip = "GameCore does not wire this automatically. See Faction System Usage guide."))
    TSoftObjectPtr<ULevelProgressionDefinition> ReputationProgression;

    // Max level of the reputation progression.
    // Used by game module wiring to map progression level → rank index.
    // Meaningful only when ReputationProgression is set.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reputation",
        meta = (EditCondition = "ReputationProgression != nullptr", ClampMin = 1))
    int32 MaxReputationLevel = 100;

    // ── Ranks ─────────────────────────────────────────────────────────────

    // Ordered list of rank tags, lowest to highest.
    // e.g. Faction.Navy.Rank.Sailor, Faction.Navy.Rank.Officer, Faction.Navy.Rank.Admiral
    // Empty = no rank system for this faction.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranks")
    TArray<FGameplayTag> RankTags;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
#endif
};
