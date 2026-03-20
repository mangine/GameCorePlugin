# URequirement — Base Class

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirement` is the abstract base class for all evaluatable conditions. It defines two evaluation contracts — imperative and event-driven — and the watcher invalidation contract. Instances are authored in Data Assets and shared. They carry no per-player or per-evaluation state.

**File:** `Requirements/Requirement.h / .cpp`

---

# Class Definition

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API URequirement : public UObject
{
    GENERATED_BODY()
public:

    // ── Imperative Evaluation ────────────────────────────────────────────────

    // Called by the consuming system when it needs to check this condition
    // right now, with an explicitly-built context.
    //
    // The subclass casts Context.Data to its expected struct type and evaluates.
    // If Context.Data is empty or the wrong type, return Fail with a clear reason.
    //
    // Default: returns Fail. Requirements that are purely event-driven can keep
    // this default and only implement EvaluateFromEvent.
    //
    // Must be const and stateless — no mutation of the URequirement instance.
    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const;

    // ── Event-Driven Evaluation ──────────────────────────────────────────────

    // Called by URequirementWatcherManager when a subscribed event arrives.
    // Context.Data contains the event payload (FInstancedStruct from the event bus).
    //
    // Subclasses may validate the event source, check delta values, or behave
    // differently from Evaluate when responding to a live event vs a snapshot query.
    //
    // Default: delegates to Evaluate(Context). Override only when event-specific
    // behaviour is needed.
    virtual FRequirementResult EvaluateFromEvent(const FRequirementContext& Context) const;

    // ── Watcher Registration ─────────────────────────────────────────────────

    // Returns the set of RequirementEvent.* tags that invalidate this requirement.
    // Called once at URequirementList::Register() time — never per-frame.
    //
    // Return empty if this requirement is never used in a watched list.
    // Tags must be in the RequirementEvent.* namespace.
    // Use AddNativeGameplayTag at module startup to cache tag handles.
    UFUNCTION(BlueprintNativeEvent, Category = "Requirement")
    void GetWatchedEvents(FGameplayTagContainer& OutEvents) const;
    virtual void GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const {}

    // ── Editor ───────────────────────────────────────────────────────────────

#if WITH_EDITOR
    // Human-readable description including configured property values.
    // Shown in the Details panel and composite tooltips.
    // Example: "MinLevel >= 20", "Has Tag: Status.QuestReady"
    // Default: returns class DisplayName.
    virtual FString GetDescription() const;
#endif
};
```

---

# Default Implementations

```cpp
FRequirementResult URequirement::Evaluate(const FRequirementContext& Context) const
{
    // Safe default. Requirements that are purely event-driven keep this.
    return FRequirementResult::Fail(
        LOCTEXT("NotImplemented", "Requirement does not support imperative evaluation."));
}

FRequirementResult URequirement::EvaluateFromEvent(const FRequirementContext& Context) const
{
    // Default: reuse imperative path. Most requirements behave the same either way.
    return Evaluate(Context);
}
```

---

# Implementing a Requirement

## Sync requirement responding to both paths

`URequirement_MinLevel` works identically whether called imperatively or from a level-change event — it reads the current level from the context struct.

```cpp
// In Leveling/Requirements/Requirement_MinLevel.h
UCLASS(DisplayName = "Minimum Player Level")
class URequirement_MinLevel : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirement",
        meta = (ClampMin = "1"))
    int32 MinLevel = 1;

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override
    {
        // Accept either a snapshot struct or an event payload — both carry level data.
        int32 CurrentLevel = 0;

        if (const FLevelingContext* Snap = Context.Data.GetPtr<FLevelingContext>())
            CurrentLevel = Snap->CurrentLevel;
        else if (const FLevelChangedEvent* Evt = Context.Data.GetPtr<FLevelChangedEvent>())
            CurrentLevel = Evt->NewLevel;
        else
            return FRequirementResult::Fail(LOCTEXT("BadContext", "Unexpected context type."));

        if (CurrentLevel < MinLevel)
        {
            return FRequirementResult::Fail(
                FText::Format(LOCTEXT("MinLevel_Fail", "Requires level {0}."),
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

## Event-only requirement (different imperative and event behaviour)

Some requirements only make sense in response to a specific event and cannot evaluate from a snapshot. They return `Fail` imperatively and implement `EvaluateFromEvent` specifically.

```cpp
UCLASS(DisplayName = "Killed Specific Enemy Type")
class URequirement_KilledEnemyType : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category = "Requirement")
    FGameplayTag RequiredEnemyTag;

    // Imperative: cannot evaluate without a kill event — always fails.
    // The owning system should not include this in lists evaluated imperatively.
    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override
    {
        return FRequirementResult::Fail(
            LOCTEXT("EventOnly", "This requirement only evaluates from kill events."));
    }

    // Event: check whether the killed enemy matches the required tag.
    virtual FRequirementResult EvaluateFromEvent(const FRequirementContext& Context) const override
    {
        const FEnemyKilledEvent* Evt = Context.Data.GetPtr<FEnemyKilledEvent>();
        if (!Evt)
            return FRequirementResult::Fail(LOCTEXT("BadEvent", "Expected FEnemyKilledEvent."));

        if (!Evt->EnemyTags.HasTag(RequiredEnemyTag))
        {
            return FRequirementResult::Fail(
                FText::Format(LOCTEXT("WrongEnemy", "Must kill enemy with tag {0}."),
                    FText::FromName(RequiredEnemyTag.GetTagName())));
        }
        return FRequirementResult::Pass();
    }

    virtual void GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Combat.EnemyKilled"));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Killed: %s"), *RequiredEnemyTag.ToString());
    }
#endif
};
```

---

# Statelessness Contract

`URequirement` instances are authored in Data Assets, loaded once, and evaluated concurrently against many players. **Prohibited** as instance variables:

- Pointers to runtime actors, components, or UObjects
- Cached results from previous evaluations
- Counters, timestamps, or any accumulated values

**Allowed** as instance (configuration) variables:

- `FGameplayTag`, `FName`, `FText` — identifiers
- `int32`, `float`, `bool` — thresholds
- `TSoftObjectPtr`, `TSoftClassPtr` — asset references loaded on demand

---

# UCLASS Specifier Reference

| Specifier | On base | On subclass | Effect |
|---|---|---|---|
| `Abstract` | Yes | **Never** | Prevents direct instantiation of `URequirement` |
| `EditInlineNew` | Yes | Inherited | Enables class picker in Details panel |
| `CollapseCategories` | Yes | Inherited | Flattens property categories |
| `DisplayName` | — | **Required** | Name shown in class picker |
| `Blueprintable` | Yes | Inherited | Allows Blueprint subclassing |

---

# Adding a New Requirement Type — Checklist

```
1. Subclass URequirement in the module that owns the data being queried.
   Leveling data  → Leveling/Requirements/
   Tag data       → Tags/Requirements/
   Quest data     → Quest/Requirements/
   Generic logic  → Requirements/

2. UCLASS macro: add DisplayName. Do NOT add Abstract.

3. Declare config properties: EditDefaultsOnly, BlueprintReadOnly.
   No mutable state. No runtime pointers.

4. Override Evaluate(FRequirementContext) if this requirement supports
   imperative checks. Cast Context.Data to the expected struct.
   Handle unknown struct type with a clear Fail reason.

5. Override EvaluateFromEvent(FRequirementContext) if event behaviour
   differs from imperative behaviour. Default delegates to Evaluate.

6. Override GetWatchedEvents_Implementation to return the
   RequirementEvent.* tags that invalidate this requirement.
   Return empty if never used in watched lists.

7. Override GetDescription() inside #if WITH_EDITOR.
   Include configured values: "MinLevel >= 20".

8. No registration step — UE reflection discovers the subclass automatically.
```
