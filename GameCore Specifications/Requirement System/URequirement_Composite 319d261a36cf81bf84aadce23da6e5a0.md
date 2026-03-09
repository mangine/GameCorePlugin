# URequirement_Composite

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirement_Composite` is the boolean logic layer of the Requirement System. It holds a tree of child `URequirement` instances and combines their results using AND, OR, or NOT logic. It is itself a `URequirement` subclass — composites can be nested arbitrarily deep, and a single `URequirement_Composite` root is the canonical way to express any multi-condition gate.

**File:** `Requirements/RequirementComposite.h / .cpp`

---

# `ERequirementOperator`

```cpp
// Controls how a URequirement_Composite combines its children's results.
UENUM(BlueprintType)
enum class ERequirementOperator : uint8
{
    // ALL children must pass. Short-circuits on first failure.
    // Evaluation order follows the Children array order.
    // FailureReason is taken from the first failing child.
    AND UMETA(DisplayName = "AND — All Must Pass"),

    // AT LEAST ONE child must pass. Short-circuits on first success.
    // Evaluation order follows the Children array order.
    // FailureReason is taken from the last evaluated child when all fail.
    OR  UMETA(DisplayName = "OR — Any Must Pass"),

    // Exactly ONE child is expected. The child's result is inverted.
    // Pass becomes Fail; Fail becomes Pass.
    // FailureReason when the NOT fails: the child's own FailureReason is discarded
    // and replaced with the composite's NotFailureReason property.
    // Children array must contain exactly one entry — validated at BeginPlay in
    // development builds.
    NOT UMETA(DisplayName = "NOT — Must Not Pass")
};
```

---

# `URequirement_Composite`

```cpp
UCLASS(DisplayName = "Composite (AND / OR / NOT)", EditInlineNew, CollapseCategories)
class GAMECORE_API URequirement_Composite : public URequirement
{
    GENERATED_BODY()

public:
    // Boolean operator applied to the Children array.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Composite")
    ERequirementOperator Operator = ERequirementOperator::AND;

    // Child requirements evaluated by this composite.
    // Each child may itself be a URequirement_Composite — arbitrary nesting is supported.
    // Instanced: each composite owns its children as embedded UObjects.
    // The Details panel shows each child inline with its own class picker and properties.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Composite")
    TArray<TObjectPtr<URequirement>> Children;

    // Failure reason returned when the NOT operator's child passes (i.e. the NOT fails).
    // The child's own FailureReason is discarded in this case — the child passed, so its
    // failure text is semantically wrong. Populate this with a player-facing explanation.
    // Example: "You must not be in combat", "Cannot board a ship already under way".
    // Ignored when Operator != NOT.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Composite",
        meta = (EditCondition = "Operator == ERequirementOperator::NOT"))
    FText NotFailureReason;

    // ── URequirement overrides ────────────────────────────────────────────────

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override;
    virtual bool IsAsync() const override;
    virtual void EvaluateAsync(
        const FRequirementContext& Context,
        TFunction<void(FRequirementResult)> OnComplete) const override;

#if WITH_EDITOR
    virtual FString GetDescription() const override;
#endif
};
```

---

# Evaluation Logic

## AND

Iterates `Children` in array order. Calls `Evaluate` on each. Returns the first failure encountered, immediately — children after the first failure are not evaluated. Returns `Pass` only if all children pass.

```cpp
FRequirementResult URequirement_Composite::Evaluate_AND(const FRequirementContext& Context) const
{
    for (const TObjectPtr<URequirement>& Child : Children)
    {
        if (!Child) continue;
        FRequirementResult Result = Child->Evaluate(Context);
        if (!Result.bPassed) return Result;  // Short-circuit: first failure wins
    }
    return FRequirementResult::Pass();
}
```

## OR

Iterates `Children` in array order. Returns `Pass` on the first child that passes — remaining children are not evaluated. If all children fail, returns the last child's failure result (most recently evaluated failure reason is the most relevant).

```cpp
FRequirementResult URequirement_Composite::Evaluate_OR(const FRequirementContext& Context) const
{
    FRequirementResult LastFailure = FRequirementResult::Fail();
    for (const TObjectPtr<URequirement>& Child : Children)
    {
        if (!Child) continue;
        FRequirementResult Result = Child->Evaluate(Context);
        if (Result.bPassed) return Result;  // Short-circuit: first pass wins
        LastFailure = Result;
    }
    return LastFailure;
}
```

## NOT

Expects exactly one child. Evaluates it and inverts the result. The child's `FailureReason` is discarded when the NOT itself fails — `NotFailureReason` is used instead.

```cpp
FRequirementResult URequirement_Composite::Evaluate_NOT(const FRequirementContext& Context) const
{
    if (Children.IsEmpty() || !Children[0])
        return FRequirementResult::Fail(LOCTEXT("NOT_NoChild", "NOT composite has no child."));

    FRequirementResult ChildResult = Children[0]->Evaluate(Context);

    if (ChildResult.bPassed)
    {
        // Child passed → NOT fails. Use NotFailureReason, not child's reason.
        return FRequirementResult::Fail(NotFailureReason);
    }
    // Child failed → NOT passes.
    return FRequirementResult::Pass();
}
```

---

# Async Evaluation

`IsAsync()` returns `true` if **any** child in the tree returns `IsAsync() == true`. This is evaluated recursively — a composite containing another composite containing an async leaf will correctly report itself as async.

```cpp
bool URequirement_Composite::IsAsync() const
{
    for (const TObjectPtr<URequirement>& Child : Children)
        if (Child && Child->IsAsync()) return true;
    return false;
}
```

`EvaluateAsync` dispatches children using `URequirementLibrary::EvaluateAllAsync` internally, respecting the composite's operator. Sync children are evaluated immediately; async children are dispatched in parallel. The composite fires its own `OnComplete` once all children have resolved.

> **Don't reach into `Children` from outside.** Consuming systems call `URequirementLibrary::EvaluateAll` on a `TArray` that contains the composite as one element. The library calls `Evaluate` or `EvaluateAsync` on the composite, which handles its children internally. Never call `Evaluate` on children directly from outside the composite.
> 

---

# Editor Behaviour

`GetDescription()` returns a compact human-readable string used by the Details panel tooltip and any editor tooling that visualises requirement trees:

- AND: `"AND [child1 desc] [child2 desc] ..."`
- OR: `"OR [child1 desc] [child2 desc] ..."`
- NOT: `"NOT [child desc]"`

This is `WITH_EDITOR` only — no runtime cost.

---

# Authoring Patterns

**Single condition — no composite needed.** If a system field accepts a `TObjectPtr<URequirement_Composite>` and you only need one condition, create a composite with `Operator = AND` and one child. One-child AND is semantically equivalent to the child alone — negligible overhead.

**AND tree (all must pass):**

```
Composite [AND]
  ├── URequirement_MinLevel   (MinLevel = 20)
  ├── URequirement_HasTag     (RequiredTag = Status.QuestReady)
  └── URequirement_QuestCompleted (QuestId = MainQuest_Chapter1)
```

**OR tree (any must pass):**

```
Composite [OR]
  ├── URequirement_HasTag  (RequiredTag = Faction.Ally.Merchant)
  └── URequirement_HasTag  (RequiredTag = Reputation.Trusted)
```

**NOT (must not have a condition):**

```
Composite [NOT]
  └── URequirement_HasTag  (RequiredTag = Status.InCombat)
NotFailureReason = "Cannot interact while in combat"
```

**Nested composite (AND of OR groups):**

```
Composite [AND]
  ├── URequirement_MinLevel  (MinLevel = 10)
  └── Composite [OR]
        ├── URequirement_QuestCompleted (QuestId = TreasureHunt)
        └── URequirement_HasTag         (RequiredTag = Perk.TreasureHunter)
```

---

# Evaluation Order and FailureReason Propagation

| Operator | Short-circuits on | FailureReason source |
| --- | --- | --- |
| AND | First failure | First failing child |
| OR | First success | Last evaluated child (all failed) |
| NOT | Never (one child) | `NotFailureReason` property |

FailureReason propagates up through nested composites unchanged — the innermost failing leaf's reason surfaces to the consuming system. Composites do not wrap or replace child failure reasons unless they are a NOT operator.

---

# Validation (Development Builds)

At `BeginPlay`, consuming systems that hold a requirement array should validate it using `URequirementLibrary::ValidateRequirements`. This traverses any `URequirement_Composite` entries in the array recursively:

```cpp
// In UInteractionComponent::BeginPlay — sync required, no async on hot path:
URequirementLibrary::ValidateRequirements(EntryConfig.EntryRequirements, /*bRequireSync=*/true);

// In quest/ability systems — async permitted:
URequirementLibrary::ValidateRequirements(QuestDef->EntryRequirements, /*bRequireSync=*/false);
```

---

# Known Limitations

**OR short-circuit discards intermediate failure reasons.** In an OR composite with three children where the first two fail, only the third (last evaluated) failure reason is propagated. If specific failure feedback per OR branch matters, the consuming system must evaluate children individually or accept this limitation.

**NOT expects exactly one child.** This is validated at `BeginPlay` in development builds. In shipping builds, a NOT composite with zero children returns `Fail` with an error message; a NOT composite with more than one child evaluates only the first and ignores the rest — this is a configuration error, not defined behaviour.

**Cycle detection is not performed.** A composite that contains itself (directly or transitively) will recurse infinitely. This cannot occur through the Details panel (instanced UObjects cannot reference themselves), but can occur if composites are constructed programmatically. Do not construct circular requirement trees.