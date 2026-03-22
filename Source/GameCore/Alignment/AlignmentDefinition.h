// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Requirements/RequirementList.h"
#include "AlignmentDefinition.generated.h"

/**
 * Defines one alignment axis.
 * Create one asset per axis — e.g. DA_Alignment_GoodEvil, DA_Alignment_LawChaos.
 * Shared across all players. Never mutated at runtime.
 */
UCLASS(BlueprintType, DisplayName = "Alignment Definition")
class GAMECORE_API UAlignmentDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    /**
     * Unique tag for this axis.
     * Convention: Alignment.<AxisName>  e.g. Alignment.GoodEvil, Alignment.LawChaos
     * Declared in the game module's DefaultGameplayTags.ini — not in GameCore.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment")
    FGameplayTag AlignmentTag;

    // ── Effective range ──────────────────────────────────────────────────────
    // The range returned to consumers via GetEffectiveAlignment.

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment|Ranges")
    float EffectiveMin = -100.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment|Ranges")
    float EffectiveMax = 100.f;

    // ── Saturated range (hysteresis buffer) ──────────────────────────────────
    // The underlying accumulated value is clamped to this range on mutation.
    // Must satisfy: SaturatedMin <= EffectiveMin and SaturatedMax >= EffectiveMax.
    // The gap between effective and saturated bounds is the hysteresis depth.

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment|Ranges")
    float SaturatedMin = -200.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment|Ranges")
    float SaturatedMax = 200.f;

    /**
     * Optional per-axis change gate.
     * Evaluated server-side before any delta is applied to this axis.
     * Null = always allowed.
     * If the list fails, the delta for this axis is skipped — other axes in the
     * same batch continue unaffected.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements",
              meta = (DisplayName = "Change Requirements"))
    TObjectPtr<URequirementList> ChangeRequirements;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
