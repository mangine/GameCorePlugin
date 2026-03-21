# UQuestTransitionRule

**File:** `Quest/StateMachine/QuestTransitionRule.h / .cpp`
**Inherits:** `UTransitionRule` (GameCore plugin)
**Module:** Quest module (game-side)

Extends `UTransitionRule` to evaluate a `URequirementList` against a `FRequirementContext*` passed as `ContextObject`. This bridges the State Machine System's transition evaluation API with the Requirement System's evaluation contract — no new evaluation logic is written.

---

## Class Declaration

```cpp
UCLASS(EditInlineNew, CollapseCategories, BlueprintType,
       meta=(DisplayName="Quest Transition Rule"))
class YOURGAME_API UQuestTransitionRule : public UTransitionRule
{
    GENERATED_BODY()
public:

    // Requirements that must all pass for this transition to fire.
    // Evaluated against the FRequirementContext* passed as ContextObject
    // by UQuestComponent::ResolveNextStage.
    //
    // Author using any URequirement subclasses appropriate for the condition:
    //   - URequirement_QuestCompleted  (prerequisite quests)
    //   - URequirement_QuestCooldown   (cadence / cooldown gates)
    //   - URequirement_ActiveQuestCount
    //   - Game-module requirements (inventory, faction standing, etc.)
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(ShowOnlyInnerProperties))
    TObjectPtr<URequirementList> Requirements;

    // UTransitionRule::Evaluate contract:
    //   Component — always nullptr when called from FindFirstPassingTransition
    //               (UQuestComponent does not host UStateMachineComponent).
    //   ContextObject — FRequirementContext* cast target.
    //                   Must never be nullptr for quest transitions.
    virtual bool Evaluate(
        UStateMachineComponent* Component,
        UObject* ContextObject) const override
    {
        if (!Requirements) return true; // no requirements = always pass

        // ContextObject is the FRequirementContext* passed by UQuestComponent::ResolveNextStage.
        // Cast via UObject wrapper — FRequirementContext is not a UObject;
        // see note below on the wrapper approach.
        const FRequirementContext* Ctx =
            ContextObject
                ? static_cast<FRequirementContext*>(ContextObject->GetUserData())
                : nullptr;

        // Fallback: if no context provided, attempt unconditional pass.
        // This should not occur in normal quest evaluation — log a warning.
        if (!Ctx)
        {
            UE_LOG(LogQuest, Warning,
                TEXT("UQuestTransitionRule::Evaluate called with null context. "
                     "Transition will pass unconditionally."));
            return true;
        }

        return Requirements->Evaluate(*Ctx).bPassed;
    }

#if WITH_EDITOR
    virtual FString GetRuleDescription() const
    {
        if (!Requirements) return TEXT("(no requirements — always pass)");
        return FString::Printf(TEXT("Requirements: %s"), *Requirements->GetName());
    }
#endif
};
```

---

## Context Passing — Implementation Note

`UTransitionRule::Evaluate` takes a `UObject* ContextObject`. `FRequirementContext` is a plain struct, not a `UObject`. Two approaches are valid:

**Option A — Wrapper UObject (recommended for type safety):**
```cpp
// In Quest/StateMachine/QuestTransitionRule.h
UCLASS()
class YOURGAME_API UQuestTransitionContext : public UObject
{
    GENERATED_BODY()
public:
    FRequirementContext Context;
};
```

```cpp
// In UQuestComponent::ResolveNextStage:
UQuestTransitionContext* CtxWrapper = NewObject<UQuestTransitionContext>(this);
CtxWrapper->Context = BuildRequirementContext();

return Def->StageGraph->FindFirstPassingTransition(
    Runtime.CurrentStageTag,
    CtxWrapper); // passed as UObject*
```

```cpp
// In UQuestTransitionRule::Evaluate:
const UQuestTransitionContext* CtxObj =
    Cast<UQuestTransitionContext>(ContextObject);
if (!CtxObj) return true;
return Requirements->Evaluate(CtxObj->Context).bPassed;
```

**Option B — Raw pointer via GetUserData (not recommended, non-UObject):**  
Avoid — requires careful lifetime management and bypasses UObject reflection.

> **Use Option A.** `UQuestTransitionContext` is a lightweight `UObject` wrapper created per-evaluation and GC'd immediately after. The cost is negligible for non-hot-path stage evaluation.

---

## Revised `UQuestComponent::ResolveNextStage`

With Option A, `ResolveNextStage` becomes:

```cpp
FGameplayTag UQuestComponent::ResolveNextStage(
    const FQuestRuntime& Runtime,
    const UQuestDefinition* Def) const
{
    // Wrap FRequirementContext in a UObject for UTransitionRule::Evaluate.
    UQuestTransitionContext* CtxWrapper =
        NewObject<UQuestTransitionContext>(GetTransientPackage());
    CtxWrapper->Context = BuildRequirementContext();

    return Def->StageGraph->FindFirstPassingTransition(
        Runtime.CurrentStageTag,
        CtxWrapper);
    // CtxWrapper is GC'd after this scope — no manual cleanup needed.
}
```

---

## Authoring Rules

- One `UQuestTransitionRule` per transition edge in the stage graph.
- If a transition should always fire (unconditional advance), leave `Requirements` null or create an empty `URequirementList` with no entries.
- Multiple outgoing transitions from the same stage express branching. `FindFirstPassingTransition` returns the first passing rule in definition order — order matters for priority.
- Rules that require a `UStateMachineComponent*` (e.g. time-in-state checks) are **not compatible** with quest stage graphs because `FindFirstPassingTransition` passes `nullptr` as the component. Use only `URequirementList`-based conditions.

---

## File Structure Addition

```
Source/PirateGame/Quest/
└── StateMachine/
    ├── QuestStateNode.h / .cpp       ← UQuestStateNode
    ├── QuestTransitionRule.h / .cpp  ← UQuestTransitionRule
    └── QuestTransitionContext.h      ← UQuestTransitionContext (Option A wrapper)
```
