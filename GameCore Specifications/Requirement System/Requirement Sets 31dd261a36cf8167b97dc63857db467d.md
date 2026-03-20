# URequirementList

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirementList` is the primary authoring unit of the Requirement System. It is a `UPrimaryDataAsset` grouping a set of `URequirement` instances under a configurable AND/OR operator with an authority declaration. Consuming systems call `Evaluate` imperatively or `RegisterWatch` to receive reactive callbacks when relevant events fire.

**File:** `Requirements/RequirementList.h / .cpp`

---

# Class Definition

```cpp
UCLASS(BlueprintType, DisplayName = "Requirement List")
class GAMECORE_API URequirementList : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // ── Authoring ───────────────────────────────────────────────────────────

    // Top-level evaluation operator.
    // AND: all requirements must pass. Short-circuits on first failure.
    // OR:  any requirement passing is sufficient. Short-circuits on first pass.
    // Place cheap checks first.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements")
    ERequirementListOperator Operator = ERequirementListOperator::AND;

    // Requirements evaluated by this list.
    // URequirement_Composite is valid here for nested AND/OR/NOT logic.
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Requirements")
    TArray<TObjectPtr<URequirement>> Requirements;

    // Network authority. Set once by the designer.
    // Mapped to EGameCoreEventScope at RegisterWatch time.
    // The watcher enforces it — callbacks are silently skipped on the wrong side.
    UPROPERTY(EditDefaultsOnly, Category = "Network")
    ERequirementEvalAuthority Authority = ERequirementEvalAuthority::ServerOnly;

    // ── Imperative Evaluation ───────────────────────────────────────────────

    // Evaluate all requirements synchronously against the provided context.
    // Always callable — does not require prior RegisterWatch.
    UFUNCTION(BlueprintCallable, Category = "Requirements")
    FRequirementResult Evaluate(const FRequirementContext& Context) const;

    // Evaluate all requirements using the event path.
    // Called internally by RegisterWatch closures when an event fires.
    FRequirementResult EvaluateFromEvent(const FRequirementContext& Context) const;

    // ── Reactive Watch Registration ───────────────────────────────────────────

    // Registers this list for reactive evaluation.
    //
    // Collects watched tags from all requirements via GetWatchedEvents.
    // Registers a closure with UGameCoreEventWatcher that:
    //   1. Wraps the event payload in FRequirementContext.
    //   2. Calls EvaluateFromEvent.
    //   3. Calls OnResult(bPassed) only if the pass/fail state changed.
    //
    // Authority is read from this->Authority and mapped to EGameCoreEventScope.
    // The watcher skips the callback silently if the net role does not match.
    //
    // OnResult captures caller context via closure — this list never inspects it.
    // Use TWeakObjectPtr for any UObject captured in OnResult.
    //
    // Returns one FEventWatchHandle covering all tag subscriptions.
    // Pass to UnregisterWatch at teardown.
    FEventWatchHandle RegisterWatch(
        const UObject* Owner,
        TFunction<void(bool /*bPassed*/)> OnResult) const;

    // Removes all subscriptions established by RegisterWatch.
    // Safe to call with an invalid handle.
    static void UnregisterWatch(
        const UObject* Owner,
        FEventWatchHandle Handle);

    // ── Internal Utilities ───────────────────────────────────────────────────────

    // Collects all RequirementEvent.* tags from every requirement in the list
    // (including composite children). Called once per RegisterWatch.
    void CollectWatchedEvents(FGameplayTagContainer& OutTags) const;

    // Returns flat list of all requirements (composite children included).
    // Used by ValidateRequirements.
    TArray<URequirement*> GetAllRequirements() const;

private:
    // Maps ERequirementEvalAuthority to EGameCoreEventScope for watcher registration.
    EGameCoreEventScope AuthorityToScope() const;
};
```

---

# `Evaluate` and `EvaluateFromEvent`

```cpp
FRequirementResult URequirementList::Evaluate(const FRequirementContext& Context) const
{
    return URequirementLibrary::EvaluateAll(Requirements, Operator, Context);
}

FRequirementResult URequirementList::EvaluateFromEvent(const FRequirementContext& Context) const
{
    return URequirementLibrary::EvaluateAllFromEvent(Requirements, Operator, Context);
}
```

---

# `RegisterWatch` — Implementation

```cpp
FEventWatchHandle URequirementList::RegisterWatch(
    const UObject* Owner,
    TFunction<void(bool)> OnResult) const
{
    if (!Owner || !OnResult) return FEventWatchHandle{};

    UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(Owner);
    if (!Watcher) return FEventWatchHandle{};

    FGameplayTagContainer WatchedTags;
    CollectWatchedEvents(WatchedTags);
    if (WatchedTags.IsEmpty()) return FEventWatchHandle{};

    // Shared last-result state: OnResult only fires when pass/fail changes.
    // TSharedPtr so it outlives the registration and is safe across calls.
    auto LastResult = MakeShared<TOptional<bool>>();

    TWeakObjectPtr<const URequirementList> WeakList = this;
    EGameCoreEventScope Scope = AuthorityToScope();

    return Watcher->Register(Owner, WatchedTags, Scope,
        [WeakList, OnResult, LastResult]
        (FGameplayTag /*Tag*/, const FInstancedStruct& Payload)
        {
            const URequirementList* L = WeakList.Get();
            if (!L) return;

            FRequirementContext Ctx;
            Ctx.Data = Payload;

            FRequirementResult Result = L->EvaluateFromEvent(Ctx);

            if (!LastResult->IsSet() || LastResult->GetValue() != Result.bPassed)
            {
                *LastResult = Result.bPassed;
                OnResult(Result.bPassed);
            }
        });
}

void URequirementList::UnregisterWatch(const UObject* Owner, FEventWatchHandle Handle)
{
    if (UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(Owner))
        Watcher->Unregister(Handle);
}
```

---

# `AuthorityToScope`

```cpp
EGameCoreEventScope URequirementList::AuthorityToScope() const
{
    switch (Authority)
    {
    case ERequirementEvalAuthority::ServerOnly:      return EGameCoreEventScope::ServerOnly;
    case ERequirementEvalAuthority::ClientOnly:      return EGameCoreEventScope::ClientOnly;
    case ERequirementEvalAuthority::ClientValidated: return EGameCoreEventScope::Both;
    }
    return EGameCoreEventScope::ServerOnly;
}
```

---

# Authoring Rules

- One `URequirement` subclass per behaviour. Vary configuration via properties, not subclasses.
- Use `URequirement_Composite` for OR/NOT logic within a list.
- If two systems need different authority for the same conditions, use two separate assets.
- Never pass or override authority at call sites. It is a designer decision encoded in the asset.
