#include "LevelProgressionDefinition.h"
#include "LevelingComponent.h"

// ── FProgressionGrantDefinition::EvaluateForLevel ─────────────────────────────

int32 FProgressionGrantDefinition::EvaluateForLevel(int32 Level) const
{
    switch (CurveType)
    {
    case EGrantCurveType::Constant:
        return ConstantAmount;

    case EGrantCurveType::CurveFloat:
        if (GrantCurve)
            return FMath::RoundToInt(GrantCurve->GetFloatValue(static_cast<float>(Level)));
        break;

    case EGrantCurveType::CurveTable:
        if (GrantCurveTableRow.IsValid())
        {
            static const FString Context(TEXT("FProgressionGrantDefinition::EvaluateForLevel"));
            if (const float* Value = GrantCurveTableRow.GetCurve(Context)->Eval(static_cast<float>(Level)))
                return FMath::RoundToInt(*Value);
        }
        break;
    }

    return 0;
}

// ── FProgressionLevelData FastArray Callbacks ─────────────────────────────────

void FProgressionLevelData::PreReplicatedRemove(const FProgressionLevelDataArray& InArraySerializer)
{
    // No-op by default. Implementing class may react to removal if needed.
}

void FProgressionLevelData::PostReplicatedAdd(const FProgressionLevelDataArray& InArraySerializer)
{
    // No-op by default.
}

void FProgressionLevelData::PostReplicatedChange(const FProgressionLevelDataArray& InArraySerializer)
{
    // No-op by default.
}

// ── FPointPoolData FastArray Callbacks ───────────────────────────────────────

void FPointPoolData::PreReplicatedRemove(const FPointPoolDataArray& InArraySerializer)
{
    // No-op by default.
}

void FPointPoolData::PostReplicatedAdd(const FPointPoolDataArray& InArraySerializer)
{
    // No-op by default.
}

void FPointPoolData::PostReplicatedChange(const FPointPoolDataArray& InArraySerializer)
{
    // No-op by default.
}

// ── ULevelProgressionDefinition ───────────────────────────────────────────────

int32 ULevelProgressionDefinition::GetXPRequiredForLevel(int32 Level) const
{
    if (Level <= 0 || Level >= MaxLevel) return 0;

    switch (XPCurveType)
    {
    case EXPCurveType::Formula:
        return EvaluateXPFormula(Level);

    case EXPCurveType::CurveFloat:
        if (XPCurveFloat)
            return FMath::RoundToInt(XPCurveFloat->GetFloatValue(static_cast<float>(Level)));
        break;

    case EXPCurveType::CurveTable:
        if (XPCurveTableRow.IsValid())
        {
            static const FString Context(TEXT("ULevelProgressionDefinition::GetXPRequiredForLevel"));
            if (const float* Value = XPCurveTableRow.GetCurve(Context)->Eval(static_cast<float>(Level)))
                return FMath::RoundToInt(*Value);
        }
        break;
    }

    return 0;
}

int32 ULevelProgressionDefinition::EvaluateXPFormula(int32 Level) const
{
    float Result = FormulaParams.Base * FMath::Pow(static_cast<float>(Level), FormulaParams.Exponent);

    // Apply optional per-level multiplier if keys are defined.
    if (FormulaParams.Multiplier.GetNumKeys() > 0)
        Result *= FormulaParams.Multiplier.Eval(static_cast<float>(Level));

    return FMath::RoundToInt(Result);
}

int32 ULevelProgressionDefinition::GetGrantAmountForLevel(int32 NewLevel) const
{
    return LevelUpGrant.EvaluateForLevel(NewLevel);
}

bool ULevelProgressionDefinition::ArePrerequisitesMet(
    const ULevelingComponent* LevelingComp,
    const AActor* Owner) const
{
    // 1. Fast-path struct check — no allocations, short-circuit on failure.
    for (const FProgressionPrerequisite& Prereq : FastPrerequisites)
    {
        if (LevelingComp->GetLevel(Prereq.ProgressionTag) < Prereq.MinLevel)
            return false;
    }

    // 2. Full URequirement evaluation — only reached if all fast prerequisites pass.
    // The context passes the Owner actor as the evaluation target.
    for (const URequirement* Req : AdvancedRequirements)
    {
        if (!Req) return false;
        FRequirementContext Ctx = FRequirementContext::Make<const AActor*>(Owner);
        if (!Req->Evaluate(Ctx).bPassed)
            return false;
    }

    return true;
}
