// Copyright GameCore Plugin. All Rights Reserved.
#include "Alignment/AlignmentDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

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
