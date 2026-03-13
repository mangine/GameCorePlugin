# UXPReductionPolicy

**Sub-page of:** [Progression System](../Progression%20System%2031bd261a36cf8139a371f3c7327ae7d8.md)

## Overview

`UXPReductionPolicy` is an **abstract DataAsset base class** that defines how XP is scaled based on the gap between a player's current level and the level of the content that triggered the grant. Concrete implementations override `Evaluate()` to provide their own reduction logic.

`UXPReductionPolicyCurve` is the default implementation, driven by a `UCurveFloat`.

Policies are assigned per-progression in `ULevelProgressionDefinition`. Different progressions (character level, crafting, faction rep) can use entirely different reduction shapes.

## File Location

```
GameCore/Source/GameCore/Progression/
└── XPReductionPolicy.h / .cpp
```

---

## UXPReductionPolicy (Abstract Base)

```cpp
UCLASS(Abstract, BlueprintType, EditInlineNew)
class GAMECORE_API UXPReductionPolicy : public UDataAsset
{
    GENERATED_BODY()

public:
    /**
     * Evaluates the XP multiplier for a given player and content level.
     *
     * @param PlayerLevel    Current level of the player in this progression.
     * @param ContentLevel   Level of the content that triggered the grant.
     *                       Pass INDEX_NONE to skip reduction — returns 1.f.
     * @return               Multiplier in range [0..2]. 1.f = no reduction.
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
    // Curve defining the XP multiplier based on level gap.
    //
    // X axis: PlayerLevel - ContentLevel
    //   Negative = underleveled  (e.g. -5: content is 5 levels above player)
    //   Zero     = same level
    //   Positive = overleveled   (e.g. +10: player is 10 levels above content)
    //
    // Y axis: multiplier, evaluated in range [0..2]
    //   Y > 1: bonus XP for underleveled content (only if bCapAtBaseline = false)
    //   Y = 1: full XP, no modification
    //   Y = 0: zero XP (trivial content)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reduction")
    TObjectPtr<UCurveFloat> ReductionCurve;

    // If true, Y values above 1.0 are clamped to 1.0.
    // Prevents underleveled content from granting bonus XP.
    // Default true — bonus XP is opt-in.
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

    const float Gap = static_cast<float>(PlayerLevel - ContentLevel);
    float Multiplier = FMath::Clamp(ReductionCurve->GetFloatValue(Gap), 0.f, 2.f);

    if (bCapAtBaseline)
    {
        Multiplier = FMath::Min(Multiplier, 1.f);
    }

    return Multiplier;
}
```

---

## Integration in ULevelProgressionDefinition

```cpp
// Optional — if null, no level-based reduction is applied for this progression.
// Assign a UXPReductionPolicyCurve asset, or a custom UXPReductionPolicy subclass.
UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "XP Reduction")
TObjectPtr<UXPReductionPolicy> ReductionPolicy;
```

The `Instanced` specifier allows designers to assign any `UXPReductionPolicy` subclass inline in the DataAsset editor without needing a separate asset file for simple cases.

---

## Integration in ULevelingComponent::ApplyXP

```cpp
void ULevelingComponent::ApplyXP(FGameplayTag ProgressionTag, int32 WarXP, int32 ContentLevel)
{
    ULevelProgressionDefinition* Def = Definitions.FindRef(ProgressionTag);
    if (!Def) return;

    const float Reduction = Def->ReductionPolicy
        ? Def->ReductionPolicy->Evaluate(GetLevel(ProgressionTag), ContentLevel)
        : 1.f;

    const int32 FinalXP = FMath::RoundToInt(WarXP * Reduction);
    LastAppliedXPDelta = FinalXP;  // cached for subsystem audit read

    if (FinalXP == 0) return;

    // ... existing XP mutation and level-up logic
}
```

---

## Custom Policy Example

To implement a stepped reduction (e.g. WoW's exact level-bracket system) instead of a smooth curve:

```cpp
UCLASS(BlueprintType, EditInlineNew)
class UXPReductionPolicyBracket : public UXPReductionPolicy
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = "Reduction")
    TArray<FLevelBracketEntry> Brackets;  // designer-configured brackets

    virtual float Evaluate(int32 PlayerLevel, int32 ContentLevel) const override;
};
```

Any subclass works as long as it overrides `Evaluate`. No changes to the subsystem or component required.

---

## Recommended GameplayTag

| Tag | Usage |
|---|---|
| `Audit.Progression.XPGain` | Audit entry for each XP grant (includes war_xp, final_xp, reduction ratio) |
