#pragma once

#include "CoreMinimal.h"
#include "StateMachine/StateMachineTypes.h"
#include "QuestStateNode.generated.h"

/**
 * Extends UStateNodeBase with terminal-state flags read by UQuestComponent after
 * stage transition evaluation. OnEnter and OnExit are intentional no-ops — all
 * quest side effects are driven by UQuestComponent, not by the node itself.
 */
UCLASS(EditInlineNew, CollapseCategories, BlueprintType,
       meta=(DisplayName="Quest Stage Node"))
class GAMECORE_API UQuestStateNode : public UStateNodeBase
{
    GENERATED_BODY()
public:

    /** When true: entering this state triggers Internal_CompleteQuest. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    bool bIsCompletionState = false;

    /** When true: entering this state triggers Internal_FailQuest. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    bool bIsFailureState = false;

    // Intentional no-ops. Quest side effects are driven by UQuestComponent.
    // No UStateMachineComponent is present on APlayerState.
    virtual void OnEnter(UStateMachineComponent* Component) override {}
    virtual void OnExit(UStateMachineComponent* Component)  override {}

#if WITH_EDITOR
    virtual FString GetNodeDescription() const
    {
        if (bIsCompletionState) return TEXT("[COMPLETE]");
        if (bIsFailureState)    return TEXT("[FAIL]");
        return TEXT("Stage");
    }

    virtual FLinearColor GetNodeColor() const
    {
        if (bIsCompletionState) return FLinearColor(0.1f, 0.7f, 0.1f); // green
        if (bIsFailureState)    return FLinearColor(0.7f, 0.1f, 0.1f); // red
        return FLinearColor::White;
    }
#endif
};
