#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "StateMachineTypes.generated.h"

class UStateMachineComponent;

/**
 * Base class for state nodes in a UStateMachineAsset.
 * Override OnEnter/OnExit to react to state changes.
 * UQuestStateNode intentionally leaves these as no-ops.
 */
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType)
class GAMECORE_API UStateNodeBase : public UObject
{
    GENERATED_BODY()
public:
    /** The gameplay tag that identifies this state in the machine. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="StateMachine")
    FGameplayTag StateTag;

    /** Tags granted while in this state. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="StateMachine")
    FGameplayTagContainer GrantedTags;

    /** If true, no transition can leave this state until the current action completes. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="StateMachine")
    bool bNonInterruptible = false;

    virtual void OnEnter(UStateMachineComponent* Component) {}
    virtual void OnExit (UStateMachineComponent* Component) {}

#if WITH_EDITOR
    virtual FString GetNodeDescription() const { return StateTag.ToString(); }
    virtual FLinearColor GetNodeColor()   const { return FLinearColor::White; }
#endif
};

/**
 * Base class for transition rules in a UStateMachineAsset.
 * Evaluate returns true if this transition should fire.
 */
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType)
class GAMECORE_API UTransitionRule : public UObject
{
    GENERATED_BODY()
public:
    /** The state this transition originates from. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="StateMachine")
    FGameplayTag FromState;

    /** The state this transition leads to. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="StateMachine")
    FGameplayTag ToState;

    /**
     * Evaluate whether this transition should fire.
     * @param Component     The driving UStateMachineComponent. May be null.
     * @param ContextObject Optional context passed by the caller.
     */
    virtual bool Evaluate(UStateMachineComponent* Component, UObject* ContextObject) const
    {
        return true;
    }

#if WITH_EDITOR
    virtual FString GetRuleDescription() const { return TEXT("(always pass)"); }
#endif
};
