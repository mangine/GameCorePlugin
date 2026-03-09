# URequirement — Base Class

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirement` is the abstract base class for all evaluatable conditions in the Requirement System. It defines the synchronous and asynchronous evaluation contracts that every concrete requirement type implements. It is never instantiated directly — only its subclasses appear in class pickers and Data Assets.

**File:** `Requirements/Requirement.h / .cpp`

---

# Class Definition

```cpp
// Abstract: cannot be instantiated directly. Only subclasses appear in class pickers.
// EditInlineNew: instances can be created and owned inline inside UPROPERTY arrays and
//               single pointers marked Instanced. This is what makes the Details panel
//               class picker work for TArray<TObjectPtr<URequirement>>.
// CollapseCategories: flattens the subclass's UPROPERTY categories one level in the
//                     Details panel, keeping the authoring UI compact.
UCLASS(Abstract, EditInlineNew, CollapseCategories)
class GAMECORE_API URequirement : public UObject
{
    GENERATED_BODY()

public:
    // ── Synchronous Evaluation ──────────────────────────────────────────────────

    // Evaluate this condition synchronously against the provided context.
    // Must return immediately. Must be const and stateless — no mutation of the
    // URequirement instance is permitted.
    //
    // Default implementation returns Fail. This is intentional:
    //   - Pure virtual would force async-only subclasses to provide a dead sync body.
    //   - Defaulting to Fail means an incomplete override is a safe, visible failure
    //     rather than a silent pass.
    //
    // Override in every sync subclass. If your subclass is async-only, keep this
    // default or override it to return a pending/checking result for client display.
    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const;

    // ── Async Evaluation ─────────────────────────────────────────────────────

    // Returns true if this requirement needs async evaluation.
    // Default: false. Override to return true alongside EvaluateAsync when the
    // condition depends on data that is not resident in memory at evaluation time.
    //
    // URequirementLibrary::EvaluateAllAsync calls this to decide whether to dispatch
    // EvaluateAsync or call Evaluate directly.
    //
    // Never return true from IsAsync() without also overriding EvaluateAsync.
    virtual bool IsAsync() const { return false; }

    // Async evaluation entry point. Called by URequirementLibrary::EvaluateAllAsync
    // only when IsAsync() returns true.
    //
    // CONTRACT — implementors must follow these rules exactly:
    //   1. OnComplete must be called EXACTLY ONCE.
    //   2. OnComplete must be called on the GAME THREAD.
    //   3. OnComplete must be called within a reasonable timeout.
    //      If the backend does not respond, fire OnComplete with Fail.
    //   4. Never capture FRequirementContext by reference in a lambda.
    //      The context is a stack variable at the call site — it may be
    //      destroyed before the callback fires. Capture specific values only.
    //
    // Default implementation: calls Evaluate() synchronously and fires OnComplete
    // immediately. Subclasses with IsAsync() == true must override this.
    virtual void EvaluateAsync(
        const FRequirementContext& Context,
        TFunction<void(FRequirementResult)> OnComplete) const;

    // ── Editor ────────────────────────────────────────────────────────────

#if WITH_EDITOR
    // Human-readable description of this requirement instance, used by editor tooling
    // and the URequirement_Composite Details panel tooltip.
    // Examples: "MinLevel >= 20", "Has Tag: Status.QuestReady", "Quest Completed: TreasureHunt"
    // Default: returns the class DisplayName. Override in every concrete subclass
    // to include the configured property values.
    virtual FString GetDescription() const;
#endif
};
```

---

# Watcher System Integration

## `bIsMonotonic`

Mark a requirement monotonic when the condition can only ever go from `false` to `true` and never revert. The watcher permanently caches the `Pass` result after the first successful evaluation, skipping all future re-evaluations for that requirement in that set.

Common monotonic requirements: `URequirement_MinLevel`, `URequirement_QuestCompleted`, achievement checks.

Do **not** mark monotonic: item possession, tag presence, time-of-day windows, cooldowns — anything the player can lose or that changes over time.

## `GetWatchedEvents`

Each module defines its invalidation event tags in `DefaultGameplayTags.ini` under the `RequirementEvent` namespace:

```
; In Leveling module's DefaultGameplayTags.ini
+GameplayTagList=(Tag="RequirementEvent.Leveling.LevelChanged", DevComment="Player level changed")

; In Inventory module's DefaultGameplayTags.ini  
+GameplayTagList=(Tag="RequirementEvent.Inventory.ItemAdded", DevComment="Item added to inventory")
+GameplayTagList=(Tag="RequirementEvent.Inventory.ItemRemoved", DevComment="Item removed from inventory")
```

Then the requirement declares which tags invalidate it:

```cpp
void URequirement_MinLevel::GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const
{
    OutEvents.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"));
}

void URequirement_HasItem::GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const
{
    OutEvents.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Inventory.ItemAdded"));
    OutEvents.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Inventory.ItemRemoved"));
}
```

The watcher subscribes to exactly these tags — no more. Precision subscriptions keep per-event cost O(watching sets), not O(all sets).

---

# Implementing a Sync Requirement

The minimal pattern for a synchronous requirement subclass:

```cpp
// UCLASS specifiers required on every concrete subclass:
//   EditInlineNew  — inherited from URequirement, but must be re-declared
//                    if you add any specifiers to the UCLASS macro.
//   DisplayName    — shown in the class picker. Always provide a human-readable name.
UCLASS(DisplayName = "Minimum Player Level")
class URequirement_MinLevel : public URequirement
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Requirement",
        meta = (ClampMin = "1"))
    int32 MinLevel = 1;

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override
    {
        // Retrieve data from context fields or subsystem lookup.
        // Never from a stored instance variable — URequirement is stateless.
        if (!Context.PlayerState) return FRequirementResult::Fail();

        ULevelingComponent* Leveling = Context.PlayerState->FindComponentByClass<ULevelingComponent>();
        if (!Leveling) return FRequirementResult::Fail();

        if (Leveling->GetCurrentLevel() < MinLevel)
        {
            return FRequirementResult::Fail(FText::Format(
                LOCTEXT("MinLevel_Failed", "Requires level {0}"),
                FText::AsNumber(MinLevel)));
        }
        return FRequirementResult::Pass();
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("MinLevel >= %d"), MinLevel);
    }
#endif
};
```

---

# Implementing an Async Requirement

See the Async Evaluation section of the [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md) main page for the full async pattern, constraints, and a complete example (`URequirement_BackendEntitlement`).

Key points:

- Override both `IsAsync()` (return `true`) and `EvaluateAsync`.
- Override `Evaluate` to return a pending/checking result for client-side display — the async path is server-only for authoritative decisions.
- `OnComplete` must be called exactly once, on the game thread, within a timeout.

---

# UCLASS Specifiers Reference

| Specifier | Set on base | Required on subclass | Effect |
| --- | --- | --- | --- |
| `Abstract` | Yes | No (do not inherit) | Prevents direct instantiation of `URequirement` |
| `EditInlineNew` | Yes | Inherited | Enables class picker and inline creation in Details panel |
| `CollapseCategories` | Yes | Inherited | Flattens property categories one level in Details panel |
| `DisplayName` | No | **Yes** | Sets the name shown in the class picker |

> **Do not re-declare `Abstract` on subclasses.** It prevents the subclass from being instantiated. Only the base class carries this specifier.
> 

---

# Statelessness Contract

`URequirement` instances are authored in Data Assets and shared across many evaluation calls. They must never carry per-evaluation or per-player state. The following are prohibited as instance variables:

- `TObjectPtr` or raw pointers to runtime actors, components, or UObjects (use `Context` fields instead)
- Cached results from previous evaluations
- Frame counters, timestamps, or accumulated values

Allowed as instance (configuration) variables:

- `FGameplayTag`, `FName`, `FText` — pure data identifiers
- `int32`, `float`, `bool` — threshold values configured in the asset
- `TSoftObjectPtr`, `TSoftClassPtr` — soft references to assets, loaded on demand

---

# Adding a New Requirement Type — Checklist

1. Subclass `URequirement` inside the module that owns the data being queried.
2. Add `DisplayName = "..."` and `Blueprintable` to the `UCLASS` macro.
3. Add `"Requirements"` to `PublicDependencyModuleNames` in the module's `Build.cs`.
4. Declare configuration properties as `EditDefaultsOnly BlueprintReadOnly UPROPERTY`.
5. Override `Evaluate` (sync) or `IsAsync` + `EvaluateAsync` (async).
6. Override `GetWatchedEvents_Implementation` to declare which `RequirementEvent.*` tags invalidate this requirement. Return empty if on-demand only.
7. Set `bIsMonotonic = true` in the class CDO constructor if the condition can never revert once passed.
8. Override `GetDescription()` inside `#if WITH_EDITOR`.
9. Done — UE's reflection system discovers the subclass automatically when its module is loaded.

**No central registry. No factory. No modification to `URequirement` itself.**