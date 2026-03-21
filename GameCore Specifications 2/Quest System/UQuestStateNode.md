# UQuestStateNode

**File:** `Quest/StateMachine/QuestStateNode.h / .cpp`
**Inherits:** `UStateNodeBase` (GameCore plugin)
**Module:** Quest module (game-side)

Extends `UStateNodeBase` with terminal-state flags read by `UQuestComponent` after stage transition evaluation. `OnEnter` and `OnExit` are intentionally no-ops — all quest side effects are driven by `UQuestComponent`, not by the node itself.

---

## Class Declaration

```cpp
UCLASS(EditInlineNew, CollapseCategories, BlueprintType,
       meta=(DisplayName="Quest Stage Node"))
class YOURGAME_API UQuestStateNode : public UStateNodeBase
{
    GENERATED_BODY()
public:

    // When true: entering this state triggers Internal_CompleteQuest.
    // Set on the final success stage in the stage graph.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    bool bIsCompletionState = false;

    // When true: entering this state triggers Internal_FailQuest.
    // Set on failure branch states in the stage graph.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    bool bIsFailureState = false;

    // OnEnter / OnExit are intentional no-ops.
    // Quest side effects (broadcasting events, updating trackers, calling
    // Internal_CompleteQuest / Internal_FailQuest) are driven by
    // UQuestComponent after FindFirstPassingTransition returns.
    // No UStateMachineComponent is present on APlayerState, so these
    // overrides would never be called at runtime — kept explicit for clarity.
    virtual void OnEnter(UStateMachineComponent* Component) override {}
    virtual void OnExit(UStateMachineComponent* Component)  override {}

#if WITH_EDITOR
    virtual FString GetNodeDescription() const override
    {
        if (bIsCompletionState) return TEXT("[COMPLETE]");
        if (bIsFailureState)    return TEXT("[FAIL]");
        return TEXT("Stage");
    }

    virtual FLinearColor GetNodeColor() const override
    {
        if (bIsCompletionState) return FLinearColor(0.1f, 0.7f, 0.1f); // green
        if (bIsFailureState)    return FLinearColor(0.7f, 0.1f, 0.1f); // red
        return FLinearColor::White;
    }
#endif
};
```

---

## Usage

`UQuestStateNode` is used exclusively inside `UStateMachineAsset` instances assigned to `UQuestDefinition::StageGraph`. It is never instantiated at runtime by `UStateMachineComponent` — `UQuestComponent` reads the asset directly via `FindFirstPassingTransition`.

```
Authoring a quest stage graph:
  1. Create UStateMachineAsset.
  2. Add UQuestStateNode for each stage.
     - Set StateTag to match UQuestStageDefinition::StageTag.
     - Set bIsCompletionState=true on the final success node.
     - Set bIsFailureState=true on failure branch nodes.
  3. Add UQuestTransitionRule on each transition edge.
  4. Assign asset to UQuestDefinition::StageGraph.
```

---

## Consistency with `UQuestStageDefinition`

`UQuestStageDefinition` also carries `bIsCompletionState` and `bIsFailureState` flags. Both must be consistent for a given `StageTag`. `UQuestDefinition::IsDataValid` should enforce this at cook time:

```cpp
// In UQuestDefinition::IsDataValid:
for (const TObjectPtr<UQuestStageDefinition>& Stage : Stages)
{
    if (!Stage) continue;
    const UQuestStateNode* Node = Cast<UQuestStateNode>(
        StageGraph ? StageGraph->FindNode(Stage->StageTag) : nullptr);
    if (!Node)
    {
        Context.AddError(FText::Format(
            LOCTEXT("MissingNode", "Stage '{0}' has no matching UQuestStateNode in StageGraph."),
            FText::FromString(Stage->StageTag.ToString())));
        continue;
    }
    if (Node->bIsCompletionState != Stage->bIsCompletionState ||
        Node->bIsFailureState    != Stage->bIsFailureState)
    {
        Context.AddError(FText::Format(
            LOCTEXT("FlagMismatch",
                "Stage '{0}': bIsCompletionState / bIsFailureState mismatch between "
                "UQuestStageDefinition and UQuestStateNode."),
            FText::FromString(Stage->StageTag.ToString())));
    }
}
```

---

## Design Notes

- `bNonInterruptible` (from `UStateNodeBase`) should be `false` on all quest stage nodes. Quest stage transitions are driven imperatively by `UQuestComponent` — there is no `UStateMachineComponent` to enforce the non-interruptible guard.
- `GrantedTags` (from `UStateNodeBase`) is unused by quest stages. `UQuestComponent` does not call `OnEnter`, so tags would never be granted.
- The editor color coding (green/red for terminal nodes) is purely cosmetic and helps designers verify the graph at a glance.
