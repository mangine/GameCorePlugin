#pragma once

#include "CoreMinimal.h"
#include "StateMachine/StateMachineTypes.h"
#include "Requirements/RequirementList.h"
#include "Quest/StateMachine/QuestTransitionContext.h"
#include "QuestTransitionRule.generated.h"

/**
 * Extends UTransitionRule to evaluate a URequirementList against a FRequirementContext*
 * passed as ContextObject (wrapped in UQuestTransitionContext).
 *
 * Bridges the State Machine System's transition evaluation API with the Requirement
 * System's evaluation contract — no new evaluation logic is written.
 */
UCLASS(EditInlineNew, CollapseCategories, BlueprintType,
       meta=(DisplayName="Quest Transition Rule"))
class GAMECORE_API UQuestTransitionRule : public UTransitionRule
{
    GENERATED_BODY()
public:

    /**
     * Requirements that must all pass for this transition to fire.
     * Evaluated against the FRequirementContext passed inside UQuestTransitionContext.
     * Leave null for an unconditional transition.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(ShowOnlyInnerProperties))
    TObjectPtr<URequirementList> Requirements;

    /**
     * UTransitionRule::Evaluate contract:
     *   Component     — always nullptr when called from FindFirstPassingTransition
     *                   (UQuestComponent does not host UStateMachineComponent).
     *   ContextObject — UQuestTransitionContext* containing FRequirementContext.
     *                   Must not be nullptr for quest transitions.
     */
    virtual bool Evaluate(
        UStateMachineComponent* Component,
        UObject* ContextObject) const override
    {
        if (!Requirements) return true; // no requirements = always pass

        const UQuestTransitionContext* CtxObj =
            Cast<UQuestTransitionContext>(ContextObject);

        if (!CtxObj)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("UQuestTransitionRule::Evaluate called without UQuestTransitionContext. "
                     "Transition will pass unconditionally."));
            return true;
        }

        return Requirements->Evaluate(CtxObj->Context).bPassed;
    }

#if WITH_EDITOR
    virtual FString GetRuleDescription() const
    {
        if (!Requirements) return TEXT("(no requirements — always pass)");
        return FString::Printf(TEXT("Requirements: %s"), *Requirements->GetName());
    }
#endif
};
