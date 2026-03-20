# URequirementLibrary

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirementLibrary` is an internal helper for `URequirementList`. It handles evaluation loop logic, operator short-circuiting, and development-time validation. Consuming systems never call this library directly — always call `List->Evaluate(Context)`.

**File:** `Requirements/RequirementLibrary.h / .cpp`

---

# Class Definition

```cpp
UCLASS()
class GAMECORE_API URequirementLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:

    // ── Evaluation ────────────────────────────────────────────────────────────

    // Evaluates all requirements using Evaluate(). Respects Operator short-circuit.
    // Returns combined FRequirementResult.
    // AND: first failure wins. OR: first pass wins.
    // Safe on empty arrays — returns Pass immediately.
    static FRequirementResult EvaluateAll(
        const TArray<TObjectPtr<URequirement>>& Requirements,
        ERequirementListOperator Operator,
        const FRequirementContext& Context);

    // Evaluates all requirements using EvaluateFromEvent(). Same short-circuit rules.
    // Called by URequirementList::NotifyEvent.
    static FRequirementResult EvaluateAllFromEvent(
        const TArray<TObjectPtr<URequirement>>& Requirements,
        ERequirementListOperator Operator,
        const FRequirementContext& Context);

    // ── Validation (dev builds only) ─────────────────────────────────────────

    // Validates a requirement array at setup time (BeginPlay or Register).
    // Returns false and logs errors if any violations are found.
    //
    // Checks:
    //   - No null entries.
    //   - NOT composites have exactly one child.
    //   - No null children at any composite nesting depth.
    //   - If ListAuthority is ClientOnly or ClientValidated: no requirement
    //     returns ServerOnly data (would silently fail on client).
    //
    // No-op and returns true in shipping builds.
    static bool ValidateRequirements(
        const TArray<TObjectPtr<URequirement>>& Requirements,
        ERequirementEvalAuthority ListAuthority = ERequirementEvalAuthority::ServerOnly);
};
```

---

# `EvaluateAll` — Implementation

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

// EvaluateAllFromEvent mirrors EvaluateAll but calls EvaluateFromEvent.
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

# Evaluation Order and Short-Circuit

- Array order is evaluation order. **Place cheap checks first.**
- The first failure reason (AND) or last failure reason (OR) surfaces to the consuming system.
- If the most player-relevant failure is not the cheapest check, reorder deliberately.

---

# `ValidateRequirements` — Checks Performed

| Check | Severity | When caught |
|---|---|---|
| Null entry in Requirements array | Error | Always |
| NOT composite with zero or >1 children | Error | Always |
| Null child inside any composite | Error | Always |
| Requirements needing server data in a ClientOnly/ClientValidated list | Warning | Dev builds only |

Call at `BeginPlay` or `Register()` time. In shipping builds this is a no-op returning `true`.

```cpp
// Example usage in a consuming system:
void UMySystem::BeginPlay()
{
    Super::BeginPlay();
    // Validate in dev builds to catch authoring errors early.
    URequirementLibrary::ValidateRequirements(
        MyRequirementList->Requirements,
        MyRequirementList->Authority);
}
```
