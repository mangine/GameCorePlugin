#include "XPReductionPolicy.h"

float UXPReductionPolicyCurve::Evaluate(int32 PlayerLevel, int32 ContentLevel) const
{
    if (!ReductionCurve || ContentLevel == INDEX_NONE)
    {
        return 1.f;
    }

    const float Gap        = static_cast<float>(PlayerLevel - ContentLevel);
    float       Multiplier = FMath::Clamp(ReductionCurve->GetFloatValue(Gap), 0.f, 2.f);

    if (bCapAtBaseline)
    {
        Multiplier = FMath::Min(Multiplier, 1.f);
    }

    return Multiplier;
}
