#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveFloat.h"
#include "XPReductionPolicy.generated.h"

/**
 * UXPReductionPolicy
 *
 * Abstract, instanced UDataAsset base class.
 * Defines how XP is scaled based on the gap between the player's current level
 * and the content level that triggered the grant.
 *
 * Policies are assigned per-progression on ULevelProgressionDefinition via the
 * Instanced specifier, allowing inline configuration in the editor without a
 * separate asset file.
 *
 * Subclasses override Evaluate() only — no changes to the subsystem or component required.
 *
 * Pass ContentLevel == INDEX_NONE to skip reduction entirely (returns 1.f).
 */
UCLASS(Abstract, BlueprintType, EditInlineNew)
class GAMECORE_API UXPReductionPolicy : public UDataAsset
{
    GENERATED_BODY()

public:
    /**
     * Returns the XP multiplier for the given player/content level pair.
     *
     * @param PlayerLevel   Current level of the player in this progression.
     * @param ContentLevel  Level of the content that triggered the grant.
     *                      Pass INDEX_NONE to skip reduction entirely (returns 1.f).
     * @return              Multiplier in [0..2]. 1.f = no reduction.
     */
    UFUNCTION(BlueprintCallable, Category = "Progression")
    virtual float Evaluate(int32 PlayerLevel, int32 ContentLevel) const
    {
        return 1.f;
    }
};

/**
 * UXPReductionPolicyCurve
 *
 * Default implementation of UXPReductionPolicy, driven by a UCurveFloat.
 *
 * Curve X axis: PlayerLevel - ContentLevel
 *   Negative = underleveled  (e.g. -5: content is 5 levels above player)
 *   Zero     = same level
 *   Positive = overleveled   (e.g. +10: player is 10 levels above content)
 *
 * Curve Y axis: multiplier in [0..2]
 *   Y > 1: bonus XP for underleveled content (only if bCapAtBaseline = false)
 *   Y = 1: full XP, no modification
 *   Y = 0: zero XP (trivial content)
 *
 * bCapAtBaseline (default true): clamps Y to 1.0 max — bonus XP is opt-in per progression.
 */
UCLASS(BlueprintType, EditInlineNew)
class GAMECORE_API UXPReductionPolicyCurve : public UXPReductionPolicy
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reduction")
    TObjectPtr<UCurveFloat> ReductionCurve;

    // If true, Y values above 1.0 are clamped to 1.0.
    // Prevents underleveled content from granting bonus XP.
    // Default true — bonus XP is opt-in per progression.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reduction")
    bool bCapAtBaseline = true;

    virtual float Evaluate(int32 PlayerLevel, int32 ContentLevel) const override;
};
