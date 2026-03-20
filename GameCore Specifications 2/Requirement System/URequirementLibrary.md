# `URequirementLibrary`

**File:** `Requirements/RequirementLibrary.h / .cpp`

`URequirementLibrary` is an **internal C++ helper** for `URequirementList`. It handles the evaluation loop, operator short-circuiting, and development-time validation. Consuming systems never call this library directly — always call `List->Evaluate(Context)` or `List->EvaluateFromEvent(Context)`.

> **Not a `UBlueprintFunctionLibrary`.** This is a plain C++ class with static methods. It is not exposed to Blueprint. Consuming systems must not depend on it directly.

---

## Class Definition

```cpp
class GAMECORE_API URequirementLibrary
{
public:
    URequirementLibrary() = delete;

    // ── Evaluation ──────────────────────────────────────────────────────────

    // Evaluates all requirements using Evaluate(). Respects Operator short-circuit.
    // AND: first failure wins. OR: first pass wins.
    // Safe on empty arrays — returns Pass immediately.
    static FRequirementResult EvaluateAll(
        const TArray<TObjectPtr<URequirement>>& Requirements,
        ERequirementListOperator Operator,
        const FRequirementContext& Context);

    // Evaluates all requirements using EvaluateFromEvent(). Same short-circuit rules.
    // Called by URequirementList::EvaluateFromEvent.
    static FRequirementResult EvaluateAllFromEvent(
        const TArray<TObjectPtr<URequirement>>& Requirements,
        ERequirementListOperator Operator,
        const FRequirementContext& Context);

    // ── Validation (dev builds only) ─────────────────────────────────────────

    // Validates a requirement array at setup time (BeginPlay or RegisterWatch).
    // Logs errors and returns false if any violations are found.
    //
    // Checks:
    //   - No null entries in the Requirements array.
    //   - NOT composites have exactly one child.
    //   - No null children at any composite nesting depth.
    //   - If ListAuthority is ClientOnly or ClientValidated: logs a warning if
    //     any requirement declares server-only data access (heuristic check).
    //
    // No-op and returns true in Shipping builds.
    static bool ValidateRequirements(
        const TArray<TObjectPtr<URequirement>>& Requirements,
        ERequirementEvalAuthority ListAuthority = ERequirementEvalAuthority::ServerOnly);
};
```

---

## `EvaluateAll` — Implementation

```cpp
FRequirementResult URequirementLibrary::EvaluateAll(
    const TArray<TObjectPtr<URequirement>>& Requirements,
    ERequirementListOperator Operator,
    const FRequirementContext& Context)
{
    if (Requirements.IsEmpty())
        return FRequirementResult::Pass();

    if (Operator == ERequirementListOperator::AND)
    {
        for (const TObjectPtr<URequirement>& Req : Requirements)
        {
            if (!Req) continue;
            FRequirementResult R = Req->Evaluate(Context);
            if (!R.bPassed) return R; // short-circuit
        }
        return FRequirementResult::Pass();
    }
    else // OR
    {
        FRequirementResult LastFailure = FRequirementResult::Fail();
        for (const TObjectPtr<URequirement>& Req : Requirements)
        {
            if (!Req) continue;
            FRequirementResult R = Req->Evaluate(Context);
            if (R.bPassed) return R; // short-circuit
            LastFailure = R;
        }
        return LastFailure;
    }
}

FRequirementResult URequirementLibrary::EvaluateAllFromEvent(
    const TArray<TObjectPtr<URequirement>>& Requirements,
    ERequirementListOperator Operator,
    const FRequirementContext& Context)
{
    if (Requirements.IsEmpty())
        return FRequirementResult::Pass();

    if (Operator == ERequirementListOperator::AND)
    {
        for (const TObjectPtr<URequirement>& Req : Requirements)
        {
            if (!Req) continue;
            FRequirementResult R = Req->EvaluateFromEvent(Context);
            if (!R.bPassed) return R;
        }
        return FRequirementResult::Pass();
    }
    else
    {
        FRequirementResult LastFailure = FRequirementResult::Fail();
        for (const TObjectPtr<URequirement>& Req : Requirements)
        {
            if (!Req) continue;
            FRequirementResult R = Req->EvaluateFromEvent(Context);
            if (R.bPassed) return R;
            LastFailure = R;
        }
        return LastFailure;
    }
}
```

---

## Evaluation Order and Short-Circuit

- Array order is evaluation order. **Place cheap checks first.**
- AND returns the first failure reason. OR returns the last failure reason (all children failed).
- If the most player-relevant failure is not the cheapest check, reorder deliberately.

---

## `ValidateRequirements` — Checks Performed

| Check | When caught |
|---|---|
| Null entry in Requirements array | Dev + Shipping |
| NOT composite with zero or >1 children | Dev only |
| Null child inside any composite | Dev only |
| ClientOnly/ClientValidated list containing requirements that query server-only data | Dev only (warning) |

```cpp
// Call at BeginPlay in the consuming system:
void UMySystem::BeginPlay()
{
    Super::BeginPlay();
#if !UE_BUILD_SHIPPING
    if (MyRequirementList)
        URequirementLibrary::ValidateRequirements(
            MyRequirementList->Requirements,
            MyRequirementList->Authority);
#endif
}
```
