# URequirement — Base Class

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirement` is the abstract base class for all evaluatable conditions in the Requirement System. It defines the synchronous and asynchronous evaluation contracts, the watcher invalidation contract, and the monotonic cache hint. It is never instantiated directly — only its subclasses appear in class pickers and Data Assets.

**File:** `Requirements/Requirement.h / .cpp`

---

# Class Definition

```cpp
// Abstract: cannot be instantiated directly. Only subclasses appear in class pickers.
// EditInlineNew: enables class picker and inline creation in Details panel.
// CollapseCategories: flattens subclass property categories one level in Details panel.
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API URequirement : public UObject
{
    GENERATED_BODY()

public:
    // ── Watcher / Cache Hint ──────────────────────────────────────────────────

    // When true: this requirement can only ever go from Fail to Pass — never back.
    // The watcher permanently caches the Pass result after the first successful
    // evaluation, skipping all future re-evaluations for that requirement.
    //
    // Common monotonic requirements: MinLevel, QuestCompleted, AchievementUnlocked.
    // Non-monotonic: item possession, tag presence, cooldowns, time windows.
    //
    // Set in CDO constructor or via the Details panel. Default: false.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirement")
    bool bIsMonotonic = false;

    // Declares which GameplayTag events invalidate this requirement's cached result.
    // Called once at RegisterSet time — never per-frame.
    //
    // Return empty if this requirement is always evaluated on-demand only
    // (i.e. never registered with the Watcher System).
    //
    // Tags must be in the RequirementEvent.* namespace. Each module defines its own
    // sub-namespace in DefaultGameplayTags.ini — never in a central file.
    //
    // Cache FGameplayTag handles via UGameplayTagsManager::AddNativeGameplayTag
    // at module startup — do not use RequestGameplayTag on a hot path.
    UFUNCTION(BlueprintNativeEvent, Category = "Requirement")
    void GetWatchedEvents(FGameplayTagContainer& OutEvents) const;
    virtual void GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const {}

    // ── Data Authority ────────────────────────────────────────────────────────

    // Declares where this requirement reads its data from at runtime.
    // Used by ValidateRequirements to detect invalid authority combinations.
    //
    //   ClientOnly  — reads only replicated data available locally.
    //                 Safe in ServerOnly, ClientOnly, and ClientValidated lists.
    //   ServerOnly  — reads non-replicated server-only data.
    //                 Must only appear in ServerOnly lists.
    //   Both        — meaningful and correct on both sides.
    //                 Safe in any list authority.
    //
    // Default: Both. Safe default — forces authors of ServerOnly requirements
    // to opt in explicitly rather than silently misfiring on clients.
    virtual ERequirementDataAuthority GetDataAuthority() const
    {
        return ERequirementDataAuthority::Both;
    }

    // ── Synchronous Evaluation ────────────────────────────────────────────────

    // Evaluate this condition synchronously against the provided context.
    // Must return immediately. Must be const — no mutation of the URequirement instance.
    //
    // Default implementation returns Fail. Intentional:
    //   - Async-only subclasses can keep this default without a dead body.
    //   - An incomplete override is a visible failure, not a silent pass.
    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const;

    // ── Async Evaluation ──────────────────────────────────────────────────────

    // Returns true if this requirement needs async evaluation.
    // Never return true without also overriding EvaluateAsync.
    virtual bool IsAsync() const { return false; }

    // Async evaluation entry point. Called by URequirementLibrary::EvaluateAllAsync
    // only when IsAsync() returns true.
    //
    // CONTRACT:
    //   1. OnComplete must be called EXACTLY ONCE.
    //   2. OnComplete must be called on the GAME THREAD.
    //   3. OnComplete must fire within a bounded timeout — use MakeGuardedCallback.
    //   4. Never capture FRequirementContext by reference. It is a stack variable.
    //
    // Default: calls Evaluate() synchronously and fires OnComplete immediately.
    // Subclasses with IsAsync() == true must override this.
    virtual void EvaluateAsync(
        const FRequirementContext& Context,
        TFunction<void(FRequirementResult)> OnComplete) const;

    // ── Editor ────────────────────────────────────────────────────────────────

#if WITH_EDITOR
    // Human-readable description of this requirement instance.
    // Include configured property values: "MinLevel >= 20", "Has Tag: Status.QuestReady".
    // Default: returns the class DisplayName.
    virtual FString GetDescription() const;
#endif

protected:
    // ── Async Safety Helper ───────────────────────────────────────────────────

    // Wraps OnComplete with three guarantees:
    //   1. Once-only: subsequent calls after the first are no-ops.
    //   2. Timeout: fires Fail(TimeoutReason) after TimeoutSeconds if no result arrives.
    //   3. Null-safe: if this URequirement is GC'd before the callback fires,
    //      the timer is cancelled and OnComplete is never called.
    //
    // Always call this before any early-return path in EvaluateAsync.
    // The timeout clock starts at the call to MakeGuardedCallback.
    TFunction<void(FRequirementResult)> MakeGuardedCallback(
        TFunction<void(FRequirementResult)> OnComplete,
        float TimeoutSeconds = 5.0f,
        FText TimeoutReason = LOCTEXT("AsyncTimeout", "Requirement check timed out.")) const;
};
```

---

# `URequirement_Persisted` — Payload-Reading Base Class

**File:** `Requirements/RequirementPersisted.h / .cpp`

An abstract intermediate class for any requirement that reads from `FRequirementContext::PersistedData`. Seals `Evaluate()` as `final` so subclasses cannot bypass the domain-key lookup. Subclasses implement `EvaluateWithPayload(Context, Payload)` instead.

```cpp
UCLASS(Abstract)
class GAMECORE_API URequirement_Persisted : public URequirement
{
    GENERATED_BODY()

public:
    // Domain tag used to look up FRequirementPayload in FRequirementContext::PersistedData.
    // Must match the key the owning system injects before calling Evaluate.
    // For quest requirements: this is the QuestId tag.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirement",
        meta = (Categories = "Quest.Id"))
    FGameplayTag PayloadKey;

    // Sealed. Subclasses must not override this — override EvaluateWithPayload instead.
    // Performs the PersistedData lookup and delegates to EvaluateWithPayload.
    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const final override;

    // Override this in subclasses. Called after the domain payload has been found.
    // Payload is the FRequirementPayload stored under PayloadKey in Context.PersistedData.
    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const PURE_VIRTUAL(
            URequirement_Persisted::EvaluateWithPayload, return FRequirementResult::Fail(););

    // Data is built from replicated runtime state — available on both sides.
    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        return ERequirementDataAuthority::Both;
    }
};
```

## Implementation

```cpp
FRequirementResult URequirement_Persisted::Evaluate(const FRequirementContext& Context) const
{
    const FRequirementPayload* Payload = Context.PersistedData.Find(PayloadKey);
    if (!Payload)
    {
        // Payload was not injected. This is an authoring error — the owning system
        // forgot to build the context. Fail loudly in non-shipping builds.
        ensureMsgf(false,
            TEXT("URequirement_Persisted: PayloadKey '%s' not found in PersistedData."
                 " Did the owning system inject its payload before calling Evaluate?"),
            *PayloadKey.ToString());
        return FRequirementResult::Fail(
            LOCTEXT("PayloadMissing", "Required progress data is unavailable."));
    }
    return EvaluateWithPayload(Context, *Payload);
}
```

---

# Implementing a Sync Requirement

```cpp
UCLASS(DisplayName = "Minimum Player Level")
class URequirement_MinLevel : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Requirement", meta = (ClampMin = "1"))
    int32 MinLevel = 1;

    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        // Level is replicated to the client — safe everywhere.
        return ERequirementDataAuthority::Both;
    }

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override
    {
        if (!Context.PlayerState)
            return FRequirementResult::Fail();

        ULevelingComponent* Leveling =
            Context.PlayerState->FindComponentByClass<ULevelingComponent>();
        if (!Leveling)
            return FRequirementResult::Fail();

        if (Leveling->GetCurrentLevel() < MinLevel)
        {
            return FRequirementResult::Fail(
                FText::Format(LOCTEXT("MinLevel_Fail", "Requires level {0}"),
                    FText::AsNumber(MinLevel)));
        }
        return FRequirementResult::Pass();
    }

    virtual void GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"));
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

```cpp
UCLASS(DisplayName = "Backend Entitlement")
class URequirement_BackendEntitlement : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category = "Requirement")
    FName EntitlementId;

    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        return ERequirementDataAuthority::ServerOnly;
    }

    virtual bool IsAsync() const override { return true; }

    virtual void EvaluateAsync(
        const FRequirementContext& Context,
        TFunction<void(FRequirementResult)> OnComplete) const override
    {
        // Wrap immediately — timeout and once-only guarantee active from this point.
        auto SafeCallback = MakeGuardedCallback(MoveTemp(OnComplete), 5.0f);

        // Capture only specific values — never capture Context by reference.
        FName CapturedId = EntitlementId;
        APlayerState* PS = Context.PlayerState;

        UEntitlementSubsystem* Subsystem =
            Context.World->GetSubsystem<UEntitlementSubsystem>();
        if (!Subsystem)
        {
            SafeCallback(FRequirementResult::Fail(
                LOCTEXT("NoSubsystem", "Entitlement service unavailable.")));
            return;
        }

        Subsystem->QueryAsync(PS, CapturedId, [SafeCallback](bool bGranted)
        {
            SafeCallback(bGranted
                ? FRequirementResult::Pass()
                : FRequirementResult::Fail(LOCTEXT("NotEntitled", "Entitlement not granted.")));
        });
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Entitlement: %s"), *EntitlementId.ToString());
    }
#endif
};
```

**Key points:**
- Call `MakeGuardedCallback` **before** any early-return path so the timeout starts immediately.
- Capture specific values from `Context` — never the struct itself by reference.
- Async requirements must not appear in watcher-registered sets (flush calls sync `Evaluate` only).

---

# Async Evaluation Flow

```
URequirementLibrary::EvaluateAllAsync(Requirements, Context, OnComplete)
  │
  ├── For each sync requirement (IsAsync() == false):
  │     Call Evaluate() immediately
  │     On Fail + FailFast mode: fire OnComplete(Fail), discard remaining ops
  │
  ├── For each async requirement (IsAsync() == true):
  │     Call EvaluateAsync(Context, WrappedCallback)
  │       → Implementor calls MakeGuardedCallback internally
  │       → Backend query dispatched
  │       → Callback fires on game thread (guaranteed by contract)
  │
  └── When all results received:
        Combine results per Operator (AND: all pass / OR: any pass)
        Fire OnComplete(CombinedResult) on game thread
```

---

# UCLASS Specifiers Reference

| Specifier | Set on base | Required on subclass | Effect |
|---|---|---|---|
| `Abstract` | Yes | **Never** — prevents instantiation | Blocks direct `URequirement` creation |
| `EditInlineNew` | Yes | Inherited | Enables class picker in Details panel |
| `CollapseCategories` | Yes | Inherited | Flattens property categories |
| `DisplayName` | No | **Required** | Sets name shown in class picker |
| `Blueprintable` | Yes (on base) | Inherited | Allows Blueprint subclassing |

> **Do not re-declare `Abstract` on subclasses.** It prevents the subclass from being instantiated.

---

# Statelessness Contract

`URequirement` instances are authored in Data Assets and shared across concurrent evaluations for many players. The following are **prohibited** as instance variables:

- Pointers to runtime actors, components, or UObjects
- Cached results from previous evaluations
- Frame counters, timestamps, or accumulated per-player values

**Allowed** as instance (configuration) variables:

- `FGameplayTag`, `FName`, `FText` — pure data identifiers
- `int32`, `float`, `bool` — threshold values configured in the asset
- `TSoftObjectPtr`, `TSoftClassPtr` — soft references to assets, loaded on demand
