#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ProgressionTypes.h"
#include "XPReductionPolicy.h"
#include "Requirements/Requirement.h"
#include "LevelProgressionDefinition.generated.h"

class ULevelingComponent;

/**
 * ULevelProgressionDefinition
 *
 * UDataAsset that fully describes a single progression type.
 * One asset per progression (e.g. DA_Progression_CharacterLevel, DA_Progression_Swordsmanship).
 * Designers configure all behaviour in the editor.
 *
 * Server-side data — never replicated to clients.
 * Clients receive only the state (level, XP) via FastArray on ULevelingComponent.
 *
 * MaxLevel changes require only a content update — no code recompile.
 * If XPCurveType is CurveFloat/CurveTable and the asset is null,
 * GetXPRequiredForLevel returns 0, causing every XP tick to appear as a level-up.
 * Always validate assets in-editor.
 */
UCLASS(BlueprintType)
class GAMECORE_API ULevelProgressionDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    // Unique tag identifying this progression (e.g. Progression.Character.Level)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FGameplayTag ProgressionTag;

    // Maximum level. Raise by editing the asset — no code change required.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level")
    int32 MaxLevel = 100;

    // If true, negative XP can cause level decrement when XP drops below the floor
    // of the current level. Level is always clamped at 1 (never below).
    // Default false: rank is permanent (GW2 / ESO reputation model).
    // Set true for PvP rating, bounty tier, or any system where demotion is intentional.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level")
    bool bAllowLevelDecrement = false;

    // ── XP Curve ──────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "XP Curve")
    EXPCurveType XPCurveType = EXPCurveType::Formula;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "XPCurveType == EXPCurveType::Formula"), Category = "XP Curve")
    FProgressionFormulaParams FormulaParams;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "XPCurveType == EXPCurveType::CurveFloat"), Category = "XP Curve")
    TObjectPtr<UCurveFloat> XPCurveFloat;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "XPCurveType == EXPCurveType::CurveTable"), Category = "XP Curve")
    FCurveTableRowHandle XPCurveTableRow;

    // ── XP Reduction ──────────────────────────────────────────────────────

    // Optional. If null, no level-gap reduction is applied.
    // Assign a UXPReductionPolicyCurve or any custom UXPReductionPolicy subclass.
    // The Instanced specifier allows inline assignment in the DataAsset editor.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "XP Reduction")
    TObjectPtr<UXPReductionPolicy> ReductionPolicy;

    // ── Level-Up Grants ───────────────────────────────────────────────────

    // Points granted to a named pool on each level-up.
    // If PoolTag is invalid or the Actor has no UPointPoolComponent, grant is silently skipped.
    // NOTE: Only one grant per level-up is currently supported (PROG-3).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grants")
    FProgressionGrantDefinition LevelUpGrant;

    // ── Prerequisites ─────────────────────────────────────────────────────

    // Fast-path: checked first, no allocations. Short-circuits if any fail.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisites")
    TArray<FProgressionPrerequisite> FastPrerequisites;

    // Full URequirement evaluation. Only checked if all FastPrerequisites pass.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Prerequisites")
    TArray<TObjectPtr<URequirement>> AdvancedRequirements;

    // ── Public API ────────────────────────────────────────────────────────

    // Returns XP required to advance from Level to Level+1.
    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetXPRequiredForLevel(int32 Level) const;

    // Returns points to grant when leveling up TO the given NewLevel.
    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetGrantAmountForLevel(int32 NewLevel) const;

    // Checks all prerequisites against the provided LevelingComponent and Actor context.
    // Fast prerequisites are evaluated first; URequirements only if fast ones pass.
    // Server-only — do not call on clients.
    bool ArePrerequisitesMet(const ULevelingComponent* LevelingComp, const AActor* Owner) const;

private:
    int32 EvaluateXPFormula(int32 Level) const;
};
