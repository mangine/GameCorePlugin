# UAlignmentDefinition

**Sub-page of:** [Alignment System](../Alignment%20System.md)

`UAlignmentDefinition` is a `UPrimaryDataAsset` that fully describes one alignment axis. Designers create one asset per axis. The asset is referenced by `UAlignmentComponent` at registration time and is never owned per-player — many players share the same definition asset.

**File:** `Alignment/AlignmentDefinition.h`

---

## Class Definition

```cpp
// One alignment axis definition. Create one asset per axis.
// Referenced by UAlignmentComponent::RegisterAlignment.
// Never modified at runtime — treat as immutable after cooking.
UCLASS(BlueprintType, DisplayName = "Alignment Definition")
class GAMECORE_API UAlignmentDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    // Unique tag for this axis. Must be unique across all registered definitions.
    // Convention: Alignment.<AxisName>  e.g. Alignment.GoodEvil, Alignment.LawChaos
    // Declared in DefaultGameplayTags.ini under the game module (not GameCore).
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment")
    FGameplayTag AlignmentTag;

    // --- Effective range ---
    // The range returned to consumers via GetEffectiveAlignment.
    // Underlying value is clamped to this range when queried.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment|Ranges")
    float EffectiveMin = -100.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment|Ranges")
    float EffectiveMax = 100.f;

    // --- Saturated range (hysteresis buffer) ---
    // The range the underlying accumulated value is clamped to on mutation.
    // Must satisfy: SaturatedMin <= EffectiveMin and SaturatedMax >= EffectiveMax.
    // The gap between effective and saturated bounds is the hysteresis depth.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment|Ranges")
    float SaturatedMin = -200.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alignment|Ranges")
    float SaturatedMax = 200.f;

    // Optional per-axis change gate.
    // If set, this list is evaluated (server-side) before any delta is applied to this axis.
    // If the list fails, the delta for this axis is skipped — other axes in the same batch continue.
    // Null = always allowed.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements",
              meta = (DisplayName = "Change Requirements"))
    TObjectPtr<URequirementList> ChangeRequirements;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
```

---

## Data Validation

Implement `IsDataValid` in the `.cpp` to catch authoring mistakes at save/cook time:

```cpp
#if WITH_EDITOR
EDataValidationResult UAlignmentDefinition::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    if (!AlignmentTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Alignment", "NoTag", "AlignmentTag must be set."));
        Result = EDataValidationResult::Invalid;
    }
    if (EffectiveMin >= EffectiveMax)
    {
        Context.AddError(NSLOCTEXT("Alignment", "BadEffective", "EffectiveMin must be less than EffectiveMax."));
        Result = EDataValidationResult::Invalid;
    }
    if (SaturatedMin > EffectiveMin)
    {
        Context.AddError(NSLOCTEXT("Alignment", "BadSaturatedMin", "SaturatedMin must be <= EffectiveMin."));
        Result = EDataValidationResult::Invalid;
    }
    if (SaturatedMax < EffectiveMax)
    {
        Context.AddError(NSLOCTEXT("Alignment", "BadSaturatedMax", "SaturatedMax must be >= EffectiveMax."));
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
- `ChangeRequirements` authority must be `ServerOnly` — alignment mutations are server-only operations. Setting it to `ClientOnly` or `ClientValidated` is a design error; `ValidateRequirements` will catch it in development builds.
- No runtime default (effective = 0 at registration) — the effective starting point is whichever of `EffectiveMin`, `EffectiveMax`, or `0` the underlying value maps to after clamping. Starting at 0 with default ranges means effective = 0 (neutral).

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

In this example the Law/Chaos axis uses a shallower hysteresis buffer (±50) than Good/Evil (±100), meaning alignment momentum drains faster on the law axis. The `ChangeRequirements` gate means lawfulness changes only apply when the player is in a civilized zone.
