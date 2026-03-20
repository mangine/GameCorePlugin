# `URequirement` — Abstract Base Class

**File:** `Requirements/Requirement.h / .cpp`

`URequirement` is the abstract base class for all evaluatable conditions. It defines two evaluation contracts (imperative and event-driven) and the watcher invalidation contract. Instances are authored inline in `URequirementList` Data Assets. They carry no per-player or per-evaluation state.

---

## Class Definition

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API URequirement : public UObject
{
    GENERATED_BODY()
public:

    // ── Imperative Evaluation ──────────────────────────────────────────────

    // Called when the consuming system needs to check this condition right now,
    // with an explicitly-built context.
    //
    // The subclass casts Context.Data to its expected struct type and evaluates.
    // If Context.Data is empty or the wrong type, return Fail with a clear reason.
    //
    // Default: returns Fail with a "not implemented" message.
    // Requirements that are purely event-driven keep this default.
    //
    // Must be const and stateless — no mutation of any instance variable.
    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const;

    // ── Event-Driven Evaluation ────────────────────────────────────────────

    // Called by the RegisterWatch closure when a subscribed event arrives.
    // Context.Data contains the event payload (FInstancedStruct from the event bus).
    //
    // Default: delegates to Evaluate(Context).
    // Override only when event-specific behaviour differs from snapshot evaluation.
    virtual FRequirementResult EvaluateFromEvent(const FRequirementContext& Context) const;

    // ── Watcher Registration ───────────────────────────────────────────────

    // Returns the set of RequirementEvent.* tags that invalidate this requirement.
    // Called once by URequirementList::CollectWatchedEvents at RegisterWatch time.
    // Never called per-frame.
    //
    // Return empty if this requirement is never used in a watched list.
    // Tags must be in the RequirementEvent.* namespace.
    UFUNCTION(BlueprintNativeEvent, Category = "Requirement")
    void GetWatchedEvents(FGameplayTagContainer& OutEvents) const;
    virtual void GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const {}

    // ── Editor ────────────────────────────────────────────────────────────

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

## Default Implementations

```cpp
FRequirementResult URequirement::Evaluate(const FRequirementContext& Context) const
{
    return FRequirementResult::Fail(
        LOCTEXT("NotImplemented", "Requirement does not support imperative evaluation."));
}

FRequirementResult URequirement::EvaluateFromEvent(const FRequirementContext& Context) const
{
    // Default: reuse imperative path.
    return Evaluate(Context);
}
```

---

## Statelessness Contract

`URequirement` instances are authored in Data Assets, loaded once, and evaluated against many players. The following are **prohibited** as instance variables:

- Pointers to runtime actors, components, or `UObject`s
- Cached results from previous evaluations
- Counters, timestamps, or any accumulated values

The following are **allowed** as configuration (authoring-time) instance variables:

- `FGameplayTag`, `FName`, `FText` — identifiers
- `int32`, `float`, `bool` — thresholds and flags
- `TSoftObjectPtr`, `TSoftClassPtr` — asset references (loaded on demand, never retained)

---

## UCLASS Specifier Reference

| Specifier | On base | On subclass | Effect |
|---|---|---|---|
| `Abstract` | Yes | **Never** | Prevents direct instantiation of `URequirement` |
| `EditInlineNew` | Yes | Inherited | Enables class picker in Details panel |
| `CollapseCategories` | Yes | Inherited | Flattens property categories |
| `DisplayName` | — | **Required** | Name shown in class picker |
| `Blueprintable` | Yes | Inherited | Allows Blueprint subclassing |

---

## Implementation Examples

### Requirement supporting both evaluation paths

`URequirement_MinLevel` works identically for imperative snapshot checks and level-change events — both context types carry the current level.

```cpp
// Leveling/Requirements/Requirement_MinLevel.h
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
        int32 CurrentLevel = 0;

        if (const FLevelingContext* Snap = Context.Data.GetPtr<FLevelingContext>())
            CurrentLevel = Snap->CurrentLevel;
        else if (const FLevelChangedEvent* Evt = Context.Data.GetPtr<FLevelChangedEvent>())
            CurrentLevel = Evt->NewLevel;
        else
            return FRequirementResult::Fail(LOCTEXT("BadCtx", "Unexpected context type."));

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

### Event-only requirement

Some requirements only make sense in response to a specific event and cannot evaluate from a snapshot. They return `Fail` imperatively and implement `EvaluateFromEvent` specifically.

```cpp
UCLASS(DisplayName = "Killed Specific Enemy Type")
class URequirement_KilledEnemyType : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category = "Requirement")
    FGameplayTag RequiredEnemyTag;

    // Imperative: cannot evaluate without a kill event. Always fails.
    // Do not include this requirement in lists used for imperative checks.
    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override
    {
        return FRequirementResult::Fail(
            LOCTEXT("EventOnly", "This requirement only evaluates from kill events."));
    }

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
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag("RequirementEvent.Combat.EnemyKilled"));
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

## New Requirement Checklist

```
1. Subclass URequirement in the module that owns the queried data.
   Leveling data  → Leveling/Requirements/
   Tag data       → Tags/Requirements/
   Quest data     → Quest/Requirements/
   Generic logic  → Requirements/

2. UCLASS: add DisplayName. Never add Abstract.

3. Config properties: EditDefaultsOnly, BlueprintReadOnly.
   No mutable state. No runtime pointers.

4. Override Evaluate() if imperative checks are needed.
   Cast Context.Data with GetPtr<T>(). Handle wrong type with clear Fail.

5. Override EvaluateFromEvent() only if event behaviour differs.
   Default delegates to Evaluate.

6. Override GetWatchedEvents_Implementation with all RequirementEvent.*
   tags that can invalidate this requirement.

7. Override GetDescription() inside #if WITH_EDITOR.

8. No registration step — UE reflection discovers the subclass.
```
