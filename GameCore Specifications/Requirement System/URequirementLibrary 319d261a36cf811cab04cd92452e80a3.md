# URequirementLibrary

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirementLibrary` is the sole entry point for evaluating requirements from consuming systems. It handles both synchronous and asynchronous evaluation, short-circuit logic, and composite tree traversal. Consuming systems never call `Evaluate` or `EvaluateAsync` directly on individual requirements — they always go through this library.

**File:** `Requirements/RequirementLibrary.h / .cpp`

---

# Supporting Types

```cpp
// Delegate fired when all async requirements in an EvaluateAllAsync call have resolved.
// The combined FRequirementResult reflects the pass/fail of the entire set.
DECLARE_DELEGATE_OneParam(FOnRequirementsEvaluated, FRequirementResult);

// Controls how EvaluateAllAsync handles a failed async result while other ops are pending.
UENUM(BlueprintType)
enum class EEvaluateAsyncMode : uint8
{
    // Wait for ALL operations to complete before firing the delegate.
    // The combined result is failed if any single requirement failed.
    // Preferred when consuming systems need to know the complete failure set
    // before deciding how to proceed.
    WaitAll  UMETA(DisplayName = "Wait All"),

    // Fire the delegate immediately on the first failure.
    // Outstanding async operations are cancelled (their OnComplete is still called
    // internally to avoid dangling callbacks, but their results are discarded).
    // Use when the first failure is enough to reject the action — no need to wait.
    FailFast UMETA(DisplayName = "Fail Fast"),
};
```

---

# Class Definition

```cpp
UCLASS()
class GAMECORE_API URequirementLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── Synchronous API ───────────────────────────────────────────────────────

    // Evaluates every requirement in the array synchronously.
    // Short-circuits on the first failure — requirements after the first failure
    // are not evaluated. Array order is evaluation order.
    //
    // Returns a combined FRequirementResult:
    //   bPassed = true only if ALL requirements pass.
    //   FailureReason = the first failing requirement's FailureReason.
    //
    // Safe to call on an empty array — returns Pass immediately.
    // Must not be called on an array that contains async requirements — async
    // requirements call Evaluate() which returns Fail by default, masking the
    // real result. Use EvaluateAllAsync instead.
    UFUNCTION(BlueprintCallable, Category = "Requirements")
    static FRequirementResult EvaluateAll(
        const TArray<URequirement*>& Requirements,
        const FRequirementContext& Context);

    // Convenience wrapper around EvaluateAll. Returns bool only — FailureReason is
    // discarded. Use when the calling system does not need to surface a failure reason.
    UFUNCTION(BlueprintCallable, Category = "Requirements")
    static bool MeetsAll(
        const TArray<URequirement*>& Requirements,
        const FRequirementContext& Context);

    // ── Asynchronous API ──────────────────────────────────────────────────────

    // Evaluates a mixed sync+async requirement array.
    // Sync requirements are evaluated immediately. Async requirements are dispatched
    // in parallel where possible. OnComplete fires on the game thread once all
    // operations have resolved.
    //
    // Safe to call on a fully-sync array — OnComplete fires on the next frame.
    // Safe to call on an empty array — OnComplete fires on the next frame with Pass.
    // Must not be called from inside Evaluate or EvaluateAsync.
    //
    // OWNERSHIP: The caller owns the FRequirementContext and must ensure it remains
    // valid until OnComplete fires. Use a TSharedPtr or ensure the context outlives
    // the async operation if there is any risk of it being destroyed first.
    static void EvaluateAllAsync(
        const TArray<URequirement*>& Requirements,
        const FRequirementContext& Context,
        FOnRequirementsEvaluated OnComplete,
        EEvaluateAsyncMode Mode = EEvaluateAsyncMode::WaitAll);

    // ── Validation (Development Builds) ───────────────────────────────────────

    // Validates a requirement array at setup time (typically BeginPlay).
    // Logs errors for each violation found and returns false if any are detected.
    //
    // Checks performed:
    //   - No null entries in the array.
    //   - For any URequirement_Composite child: NOT composites have exactly one child,
    //     no null children at any nesting level.
    //   - If bRequireSync == true: no async requirements anywhere in the array or
    //     any nested composite tree. Use bRequireSync = true for interaction entries
    //     and any system that cannot tolerate async evaluation on its hot path.
    //
    // Only compiled in development builds (WITH_EDITOR || !UE_BUILD_SHIPPING).
    // In shipping builds this function is a no-op returning true.
    static bool ValidateRequirements(
        const TArray<URequirement*>& Requirements,
        bool bRequireSync = false);
};
```

---

# Evaluation Order and Short-Circuit Behaviour

`EvaluateAll` processes the array left-to-right and stops at the first failure. This has two implications:

**Performance.** Cheap, fast checks should come first in the array. Expensive checks (component lookups, container iterations) should come last. Async checks must come last and should be avoided on hot-path evaluation sites.

**FailureReason shown to the player.** The first failing requirement's reason is what surfaces in UI. If the most player-relevant failure is not the cheapest check, you may want to reorder deliberately — or accept that a technical gate (tag check) precedes a friendlier reason (level requirement).

**OR and NOT logic.** The flat array is always AND — every element must pass. When a specific entry needs OR or NOT logic, add a `URequirement_Composite` as one element of the array with the appropriate operator. The composite handles its children internally; `EvaluateAll` treats it as a single opaque requirement.

```cpp
// Example: flat AND array where one slot needs OR logic.
// Array:
//   [0] URequirement_MinLevel      (MinLevel = 10)        ← cheap, first
//   [1] URequirement_Composite     (OR)                   ← complex, last
//         ├── URequirement_QuestCompleted (TreasureHunt)
//         └── URequirement_HasTag        (Perk.TreasureHunter)
```

---

# Using EvaluateAllAsync

```cpp
// Construct context server-side from the RPC connection.
FRequirementContext Ctx;
Ctx.PlayerState = GetPlayerState();
Ctx.World       = GetWorld();
Ctx.Instigator  = GetPawn();

// FailFast: reject as soon as any requirement fails.
URequirementLibrary::EvaluateAllAsync(
    Definition->EntryRequirements,
    Ctx,
    FOnRequirementsEvaluated::CreateUObject(this, &UMySystem::OnRequirementsResolved),
    EEvaluateAsyncMode::FailFast);

// The system must not proceed until OnRequirementsResolved fires.
void UMySystem::OnRequirementsResolved(FRequirementResult Result)
{
    if (!Result.bPassed)
    {
        // Send Result.FailureReason to client via targeted RPC.
        return;
    }
    // Proceed with the gated action.
}
```

---

# Known Limitations

**`EvaluateAllAsync` has no cancellation token.** If the consuming system is destroyed before `OnComplete` fires, the delegate fires on a stale object. Guard with `TWeakObjectPtr` capture in the lambda, or bind via `CreateUObject` which automatically becomes a no-op if the object is GC'd before the callback.

**`EvaluateAllAsync` has no global timeout.** Each async requirement is responsible for its own timeout. If an async requirement stalls indefinitely, `OnComplete` never fires. The consuming system has no built-in escape hatch. Future improvement: add a timeout parameter to `EvaluateAllAsync`.

**`EvaluateAll` silently degrades on async requirements.** Calling `EvaluateAll` on an array containing async requirements calls their `Evaluate()` override, which returns `Fail` by default. This looks like a failed requirement but is actually a missing async evaluation. `ValidateRequirements` with `bRequireSync = true` catches this at setup time in development builds.