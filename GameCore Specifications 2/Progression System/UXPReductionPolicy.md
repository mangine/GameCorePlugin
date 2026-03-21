# UXPReductionPolicy

**File:** `GameCore/Source/GameCore/Progression/XPReductionPolicy.h/.cpp`

## Overview

`UXPReductionPolicy` is an **abstract, instanced `UDataAsset` base class** defining how XP is scaled based on the gap between a player's current level and the content level that triggered the grant.

`UXPReductionPolicyCurve` is the default implementation, driven by a `UCurveFloat`. Custom implementations override `Evaluate()` only — no changes to the subsystem or component required.

Policies are assigned per-progression on `ULevelProgressionDefinition` via the `Instanced` specifier, allowing inline configuration in the editor without a separate asset file.

---

## UXPReductionPolicy (Abstract Base)

```cpp
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
```

---

## UXPReductionPolicyCurve (Default Implementation)

```cpp
UCLASS(BlueprintType, EditInlineNew)
class GAMECORE_API UXPReductionPolicyCurve : public UXPReductionPolicy
{
    GENERATED_BODY()

public:
    // Curve defining the XP multiplier from level gap.
    //
    // X axis: PlayerLevel - ContentLevel
    //   Negative = underleveled  (e.g. -5: content is 5 levels above player)
    //   Zero     = same level
    //   Positive = overleveled   (e.g. +10: player is 10 levels above content)
    //
    // Y axis: multiplier in [0..2]
    //   Y > 1: bonus XP for underleveled content (only if bCapAtBaseline = false)
    //   Y = 1: full XP, no modification
    //   Y = 0: zero XP (trivial content)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reduction")
    TObjectPtr<UCurveFloat> ReductionCurve;

    // If true, Y values above 1.0 are clamped to 1.0.
    // Prevents underleveled content from granting bonus XP.
    // Default true — bonus XP is opt-in per progression.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reduction")
    bool bCapAtBaseline = true;

    virtual float Evaluate(int32 PlayerLevel, int32 ContentLevel) const override;
};
```

```cpp
float UXPReductionPolicyCurve::Evaluate(int32 PlayerLevel, int32 ContentLevel) const
{
    if (!ReductionCurve || ContentLevel == INDEX_NONE)
    {
        return 1.f;
    }

    const float Gap      = static_cast<float>(PlayerLevel - ContentLevel);
    float       Multiplier = FMath::Clamp(ReductionCurve->GetFloatValue(Gap), 0.f, 2.f);

    if (bCapAtBaseline)
    {
        Multiplier = FMath::Min(Multiplier, 1.f);
    }

    return Multiplier;
}
```

---

## Integration in ULevelingComponent::ApplyXP

```cpp
// Inside ULevelingComponent::ApplyXP:
const float Reduction = Def->ReductionPolicy
    ? Def->ReductionPolicy->Evaluate(GetLevel(ProgressionTag), ContentLevel)
    : 1.f;

const int32 FinalXP = FMath::RoundToInt(WarXP * Reduction);
```

If `ReductionPolicy` is null, `FinalXP == WarXP` — no reduction applied.

---

## Custom Policy Example (Stepped Brackets)

```cpp
USTRUCT(BlueprintType)
struct FLevelBracketEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) int32 MinGap  = 0;
    UPROPERTY(EditAnywhere) int32 MaxGap  = 5;
    UPROPERTY(EditAnywhere) float Multiplier = 1.f;
};

UCLASS(BlueprintType, EditInlineNew)
class UXPReductionPolicyBracket : public UXPReductionPolicy
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = "Reduction")
    TArray<FLevelBracketEntry> Brackets;

    virtual float Evaluate(int32 PlayerLevel, int32 ContentLevel) const override
    {
        if (ContentLevel == INDEX_NONE) return 1.f;
        const int32 Gap = PlayerLevel - ContentLevel;
        for (const FLevelBracketEntry& B : Brackets)
        {
            if (Gap >= B.MinGap && Gap <= B.MaxGap)
                return B.Multiplier;
        }
        return 0.f;
    }
};
```

---

## Recommended Audit Tag

| Tag | Usage |
|---|---|
| `Audit.Progression.XPGain` | Audit entry per XP grant (war_xp, final_xp, reduction_ratio, content_level) |
