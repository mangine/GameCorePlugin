# `URequirement_Composite`

**File:** `Requirements/RequirementComposite.h / .cpp`

`URequirement_Composite` is the boolean logic layer. It holds a tree of child `URequirement` instances and combines their results using AND, OR, or NOT. It is itself a `URequirement` subclass — composites nest arbitrarily.

---

## Class Definition

```cpp
UCLASS(DisplayName = "Composite (AND / OR / NOT)", EditInlineNew, CollapseCategories)
class GAMECORE_API URequirement_Composite : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Composite")
    ERequirementOperator Operator = ERequirementOperator::AND;

    // Child requirements. Each may itself be a URequirement_Composite.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Composite")
    TArray<TObjectPtr<URequirement>> Children;

    // Failure reason used when NOT fails (i.e. the child passed).
    // The child's own FailureReason is semantically wrong here — the child passed.
    // Ignored when Operator != NOT.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Composite",
        meta = (EditCondition = "Operator == ERequirementOperator::NOT"))
    FText NotFailureReason;

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override;
    virtual FRequirementResult EvaluateFromEvent(const FRequirementContext& Context) const override;

#if WITH_EDITOR
    virtual FString GetDescription() const override;
#endif
};
```

---

## Evaluation Logic

```cpp
FRequirementResult URequirement_Composite::Evaluate(const FRequirementContext& Context) const
{
    switch (Operator)
    {
    case ERequirementOperator::AND:
    {
        for (const TObjectPtr<URequirement>& Child : Children)
        {
            if (!Child) continue;
            FRequirementResult R = Child->Evaluate(Context);
            if (!R.bPassed) return R; // short-circuit: first failure wins
        }
        return FRequirementResult::Pass();
    }
    case ERequirementOperator::OR:
    {
        FRequirementResult LastFailure = FRequirementResult::Fail();
        for (const TObjectPtr<URequirement>& Child : Children)
        {
            if (!Child) continue;
            FRequirementResult R = Child->Evaluate(Context);
            if (R.bPassed) return R; // short-circuit: first pass wins
            LastFailure = R;
        }
        return LastFailure;
    }
    case ERequirementOperator::NOT:
    {
        if (Children.IsEmpty() || !Children[0])
            return FRequirementResult::Fail(
                LOCTEXT("NOT_NoChild", "NOT composite has no child."));
        FRequirementResult R = Children[0]->Evaluate(Context);
        return R.bPassed
            ? FRequirementResult::Fail(NotFailureReason) // child passed → NOT fails
            : FRequirementResult::Pass();                // child failed → NOT passes
    }
    }
    return FRequirementResult::Fail();
}

FRequirementResult URequirement_Composite::EvaluateFromEvent(
    const FRequirementContext& Context) const
{
    // Mirror of Evaluate but calls EvaluateFromEvent on children,
    // allowing event-only children to participate correctly.
    switch (Operator)
    {
    case ERequirementOperator::AND:
    {
        for (const TObjectPtr<URequirement>& Child : Children)
        {
            if (!Child) continue;
            FRequirementResult R = Child->EvaluateFromEvent(Context);
            if (!R.bPassed) return R;
        }
        return FRequirementResult::Pass();
    }
    case ERequirementOperator::OR:
    {
        FRequirementResult LastFailure = FRequirementResult::Fail();
        for (const TObjectPtr<URequirement>& Child : Children)
        {
            if (!Child) continue;
            FRequirementResult R = Child->EvaluateFromEvent(Context);
            if (R.bPassed) return R;
            LastFailure = R;
        }
        return LastFailure;
    }
    case ERequirementOperator::NOT:
    {
        if (Children.IsEmpty() || !Children[0])
            return FRequirementResult::Fail(
                LOCTEXT("NOT_NoChild", "NOT composite has no child."));
        FRequirementResult R = Children[0]->EvaluateFromEvent(Context);
        return R.bPassed
            ? FRequirementResult::Fail(NotFailureReason)
            : FRequirementResult::Pass();
    }
    }
    return FRequirementResult::Fail();
}
```

---

## FailureReason Propagation

| Operator | Short-circuits on | FailureReason source |
|---|---|---|
| AND | First failure | First failing child |
| OR | First success | Last evaluated child (all failed) |
| NOT | Never (one child) | `NotFailureReason` property |

FailureReason propagates up unchanged through nested composites. The innermost failing leaf's reason surfaces to the consuming system. Composites never wrap or replace child failure reasons except in the NOT case.

---

## Authoring Examples

```
// Level AND (quest complete OR has perk)
Composite [AND]
  ├── URequirement_MinLevel        (MinLevel = 20)
  └── Composite [OR]
        ├── URequirement_QuestCompleted (QuestId = TreasureHunt)
        └── URequirement_HasTag         (Tag = Perk.TreasureHunter)

// NOT in combat
Composite [NOT]
  └── URequirement_HasTag  (Tag = Status.InCombat)
NotFailureReason = "Cannot interact while in combat"
```

---

## Validation Notes

- NOT composites must have exactly one child. `ValidateRequirements` enforces this in dev builds.
- Null children at any nesting depth are caught by `ValidateRequirements`.
- **Cycle detection is not performed at runtime.** Circular composites constructed programmatically will recurse infinitely. The Details panel cannot produce cycles because instanced `UObject`s cannot self-reference. Programmatic construction must guard against this.
