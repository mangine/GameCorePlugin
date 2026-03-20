# UAlignmentDefinition

**File:** `GameCore/Source/GameCore/Alignment/AlignmentDefinition.h` / `.cpp`

A `UPrimaryDataAsset` that fully describes one alignment axis. One asset per axis. Never modified at runtime — treat as immutable after cooking. Never owned per-player; many players share the same definition asset.

---

## Class Declaration

```cpp
// AlignmentDefinition.h
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
```

---

## `IsDataValid` Implementation

```cpp
// AlignmentDefinition.cpp
#if WITH_EDITOR
EDataValidationResult UAlignmentDefinition::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    if (!AlignmentTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Alignment", "NoTag",
            "AlignmentTag must be set."));
        Result = EDataValidationResult::Invalid;
    }
    if (EffectiveMin >= EffectiveMax)
    {
        Context.AddError(NSLOCTEXT("Alignment", "BadEffective",
            "EffectiveMin must be strictly less than EffectiveMax."));
        Result = EDataValidationResult::Invalid;
    }
    if (SaturatedMin > EffectiveMin)
    {
        Context.AddError(NSLOCTEXT("Alignment", "BadSaturatedMin",
            "SaturatedMin must be <= EffectiveMin."));
        Result = EDataValidationResult::Invalid;
    }
    if (SaturatedMax < EffectiveMax)
    {
        Context.AddError(NSLOCTEXT("Alignment", "BadSaturatedMax",
            "SaturatedMax must be >= EffectiveMax."));
        Result = EDataValidationResult::Invalid;
    }

    return Result;
}
#endif
```

---

## Authoring Rules

- One asset per axis. Never reuse an asset for two axes.
- `AlignmentTag` must be unique across all definitions registered on a single `UAlignmentComponent`.
- Axis tags belong in the **game module's** `DefaultGameplayTags.ini`, not in `GameCore`. The system is tag-driven; it has no awareness of specific axes.
- `ChangeRequirements` authority must be `ServerOnly` — alignment mutations are server-only operations. Use `ValidateRequirements` in dev builds to catch misconfigurations.
- Default starting effective value is `0` (neutral), assuming `EffectiveMin < 0 < EffectiveMax`.

---

## Example Asset Configuration

| Property | Good/Evil Axis | Law/Chaos Axis |
|---|---|---|
| `AlignmentTag` | `Alignment.GoodEvil` | `Alignment.LawChaos` |
| `EffectiveMin` | -100 | -100 |
| `EffectiveMax` | 100 | 100 |
| `SaturatedMin` | -200 | -150 |
| `SaturatedMax` | 200 | 150 |
| `ChangeRequirements` | `None` | `DA_Req_IsInCivilizedZone` |

The Law/Chaos axis uses a shallower hysteresis buffer (±50 vs ±100), meaning alignment momentum drains faster. The `ChangeRequirements` gate means lawfulness changes only apply when the player is in a civilized zone.
