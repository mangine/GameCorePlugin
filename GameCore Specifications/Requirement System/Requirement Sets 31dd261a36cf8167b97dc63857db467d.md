# URequirementList

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirementList` is the primary authoring unit of the Requirement System. It is a `UPrimaryDataAsset` grouping a set of `URequirement` instances under a configurable AND/OR operator with an authority declaration. Consuming systems hold a `TObjectPtr<URequirementList>`, call `Evaluate` imperatively, or register for reactive evaluation via the watcher manager.

**File:** `Requirements/RequirementList.h / .cpp`

---

# Class Definition

```cpp
// Delegate fired when the overall pass/fail state of this list changes
// after a watcher-triggered evaluation.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnRequirementResultChanged, bool /*bPassed*/);

UCLASS(BlueprintType, DisplayName = "Requirement List")
class GAMECORE_API URequirementList : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // ── Authoring ─────────────────────────────────────────────────────────────

    // Top-level evaluation operator.
    // AND: all requirements must pass. Short-circuits on first failure.
    // OR:  any requirement passing is sufficient. Short-circuits on first pass.
    // Ordering matters: place cheap checks first.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements")
    ERequirementListOperator Operator = ERequirementListOperator::AND;

    // Requirements evaluated by this list.
    // URequirement_Composite is valid here for nested AND/OR/NOT logic.
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Requirements")
    TArray<TObjectPtr<URequirement>> Requirements;

    // Network authority. Set once by the designer.
    // The watcher manager enforces this — lists are silently skipped
    // on the wrong network side.
    UPROPERTY(EditDefaultsOnly, Category = "Network")
    ERequirementEvalAuthority Authority = ERequirementEvalAuthority::ServerOnly;

    // ── Imperative Evaluation ─────────────────────────────────────────────────

    // Evaluate all requirements synchronously against the provided context.
    // Returns combined FRequirementResult respecting Operator.
    // Always callable — does not require prior Register().
    UFUNCTION(BlueprintCallable, Category = "Requirements")
    FRequirementResult Evaluate(const FRequirementContext& Context) const;

    // ── Reactive Evaluation ───────────────────────────────────────────────────

    // Fired after a watcher-triggered evaluation when the overall pass/fail
    // state changes. Bind before calling Register().
    // MulticastDelegate — multiple systems may bind to the same list.
    FOnRequirementResultChanged OnResultChanged;

    // Registers this list with the watcher manager in the given world.
    // Collects all GetWatchedEvents tags from every requirement and subscribes.
    // Performs an initial evaluation immediately to establish baseline state.
    // Safe to call multiple times — subsequent calls are no-ops if already registered.
    UFUNCTION(BlueprintCallable, Category = "Requirements")
    void Register(UWorld* World);

    // Unregisters from the watcher manager. Clears event subscriptions.
    // Does not unbind OnResultChanged delegates — caller is responsible.
    UFUNCTION(BlueprintCallable, Category = "Requirements")
    void Unregister(UWorld* World);

    // ── Internal (called by URequirementWatcherManager) ────────────────────────

    // Called by the watcher manager when a subscribed event tag fires.
    // Wraps the event payload in FRequirementContext and calls EvaluateFromEvent
    // on all requirements. Fires OnResultChanged if pass/fail state changed.
    void NotifyEvent(FGameplayTag EventTag, const FInstancedStruct& EventPayload);

    // Returns all watched tags collected from Requirements (including composite children).
    // Called once at Register() time.
    void CollectWatchedEvents(FGameplayTagContainer& OutTags) const;

    // Returns flat list of all requirements (composite children included).
    // Used by ValidateRequirements.
    TArray<URequirement*> GetAllRequirements() const;

private:
    // Tracks last known pass/fail so NotifyEvent only fires OnResultChanged
    // when the result actually changes.
    bool bLastResult = false;
    bool bHasEvaluatedOnce = false;
};
```

---

# `Evaluate` — Implementation

```cpp
FRequirementResult URequirementList::Evaluate(const FRequirementContext& Context) const
{
    return URequirementLibrary::EvaluateAll(Requirements, Operator, Context);
}
```

Simple delegation to the library. The library handles operator logic and short-circuiting.

---

# `NotifyEvent` — Implementation

```cpp
void URequirementList::NotifyEvent(FGameplayTag EventTag,
                                   const FInstancedStruct& EventPayload)
{
    // Wrap event payload in FRequirementContext.
    FRequirementContext Ctx;
    Ctx.Data = EventPayload;

    // Evaluate using EvaluateFromEvent path.
    FRequirementResult Result =
        URequirementLibrary::EvaluateAllFromEvent(Requirements, Operator, Ctx);

    // Fire delegate only if result changed.
    if (!bHasEvaluatedOnce || Result.bPassed != bLastResult)
    {
        bLastResult = Result.bPassed;
        bHasEvaluatedOnce = true;
        OnResultChanged.Broadcast(Result.bPassed);
    }
}
```

**Note on `bLastResult` and `bHasEvaluatedOnce`:** These are the only mutable fields on the list. They exist to prevent consuming systems from reacting to no-op events (same result as before). They are intentionally not per-consumer — if multiple systems bind `OnResultChanged`, they all share the same pass/fail tracking. This is correct because the list represents one logical gate; multiple observers of the same gate should agree on its state.

---

# `Register` / `Unregister` — Implementation

```cpp
void URequirementList::Register(UWorld* World)
{
    URequirementWatcherManager* Mgr =
        World->GetSubsystem<URequirementWatcherManager>();
    if (!Mgr) return;

    FGameplayTagContainer WatchedTags;
    CollectWatchedEvents(WatchedTags);
    Mgr->RegisterList(this, WatchedTags);

    // Initial evaluation — establish baseline so OnResultChanged
    // fires correctly on the first real event.
    // Uses an empty context since we have no event payload yet.
    // Requirements that cannot evaluate without event data will return Fail,
    // which is the correct baseline for event-only requirements.
    FRequirementContext EmptyCtx;
    FRequirementResult Initial = Evaluate(EmptyCtx);
    bLastResult = Initial.bPassed;
    bHasEvaluatedOnce = true;
}

void URequirementList::Unregister(UWorld* World)
{
    URequirementWatcherManager* Mgr =
        World->GetSubsystem<URequirementWatcherManager>();
    if (Mgr) Mgr->UnregisterList(this);
}
```

---

# Authoring Rules

- One `URequirement` subclass per behaviour. Vary configuration via properties, not subclasses.
- Use `URequirement_Composite` children for OR/NOT logic within a list that has a top-level AND operator.
- If two systems need different authority for the same conditions, use two separate assets.
- Never add an authority override parameter to `Register()` or `Evaluate()`. Authority is a designer decision encoded in the asset.
