# ULevelProgressionDefinition

**File:** `GameCore/Source/GameCore/Progression/LevelProgressionDefinition.h/.cpp`

## Overview

`ULevelProgressionDefinition` is a `UDataAsset` that fully describes a single progression type. One asset per progression (e.g. `DA_Progression_CharacterLevel`, `DA_Progression_Swordsmanship`, `DA_Progression_PirateFactionRep`). Designers configure all behavior in the editor.

The asset is loaded at runtime by `ULevelingComponent` when `RegisterProgression` is called, and stored in a server-side `TMap` keyed by `ProgressionTag`. It is **never replicated** — clients receive only the state (level, XP) via FastArray.

---

## Dependencies
- `UXPReductionPolicy` — optional instanced policy for level-gap XP scaling
- `URequirement` (Requirement System) — optional advanced prerequisites
- `UCurveFloat`, `FCurveTableRowHandle` — optional XP curve assets

---

## Class Definition

```cpp
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

    // If true, negative XP can cause level decrement when XP drops below the
    // floor of the current level. Level is always clamped at 1 (never below).
    // Default false: rank is permanent (GW2 / ESO reputation model).
    // Set true for PvP rating, bounty tier, or any system where demotion is
    // intentional design.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level")
    bool bAllowLevelDecrement = false;

    // -------------------------------------------------------------------------
    // XP Curve
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "XP Curve")
    EXPCurveType XPCurveType = EXPCurveType::Formula;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "XPCurveType == EXPCurveType::Formula"), Category = "XP Curve")
    FProgressionFormulaParams FormulaParams;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "XPCurveType == EXPCurveType::CurveFloat"), Category = "XP Curve")
    TObjectPtr<UCurveFloat> XPCurveFloat;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "XPCurveType == EXPCurveType::CurveTable"), Category = "XP Curve")
    FCurveTableRowHandle XPCurveTableRow;

    // -------------------------------------------------------------------------
    // XP Reduction
    // -------------------------------------------------------------------------

    // Optional. If null, no level-gap reduction is applied.
    // Assign a UXPReductionPolicyCurve or any custom UXPReductionPolicy subclass.
    // The Instanced specifier allows inline assignment in the DataAsset editor.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "XP Reduction")
    TObjectPtr<UXPReductionPolicy> ReductionPolicy;

    // -------------------------------------------------------------------------
    // Level-Up Grants
    // -------------------------------------------------------------------------

    // Points granted to a named pool on each level-up.
    // If PoolTag is invalid or the Actor has no UPointPoolComponent, grant is silently skipped.
    // NOTE: Only one grant per level-up is currently supported (PROG-3).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grants")
    FProgressionGrantDefinition LevelUpGrant;

    // -------------------------------------------------------------------------
    // Prerequisites
    // -------------------------------------------------------------------------

    // Fast-path: checked first, no allocations. Short-circuits if any fail.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisites")
    TArray<FProgressionPrerequisite> FastPrerequisites;

    // Full URequirement evaluation. Only checked if all FastPrerequisites pass.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Prerequisites")
    TArray<TObjectPtr<URequirement>> AdvancedRequirements;

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

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
```

---

## GetXPRequiredForLevel

```cpp
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
```

---

## GetGrantAmountForLevel

```cpp
int32 ULevelProgressionDefinition::GetGrantAmountForLevel(int32 NewLevel) const
{
    return LevelUpGrant.EvaluateForLevel(NewLevel);
}
```

---

## ArePrerequisitesMet

```cpp
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
    for (const URequirement* Req : AdvancedRequirements)
    {
        if (!Req || !Req->IsMet(Owner))
            return false;
    }

    return true;
}
```

---

## Important Notes

- `ULevelProgressionDefinition` is **server-side data** — never replicated to clients.
- `MaxLevel` changes require updating the data asset and re-shipping it as a content update. No code recompile.
- If `XPCurveType` is `CurveFloat` or `CurveTable` and the referenced asset is null/invalid, `GetXPRequiredForLevel` returns `0`, which would cause the component to treat every XP tick as a level-up. Always validate assets in-editor.
- `LevelUpGrant.PoolTag` may be an invalid tag if no grant is desired — `ULevelingComponent::GrantPointsForLevel` checks for this and silently skips the grant.
