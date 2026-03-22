#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StateMachine/StateMachineTypes.h"
#include "StateMachineAsset.generated.h"

class UStateMachineComponent;

/**
 * A data-driven state machine asset.
 * Defines a graph of UStateNodeBase nodes and UTransitionRule transitions.
 *
 * Used by the Quest System via FindFirstPassingTransition to drive stage progression
 * without a UStateMachineComponent on the player state.
 */
UCLASS(BlueprintType)
class GAMECORE_API UStateMachineAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    /** The state tag that is active when the machine first starts. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="StateMachine")
    FGameplayTag EntryStateTag;

    /** All nodes (states) in this machine. */
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="StateMachine")
    TArray<TObjectPtr<UStateNodeBase>> Nodes;

    struct FTransitionEdge
    {
        FGameplayTag          FromState;
        FGameplayTag          ToState;
        TObjectPtr<UTransitionRule> Rule;
    };

    /** All transition edges. */
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="StateMachine")
    TArray<TObjectPtr<UTransitionRule>> Transitions;

    // -------------------------------------------------------------------------

    /**
     * Finds and returns the first transition that passes from the given state.
     * Evaluates transitions in definition order — order matters for priority.
     * @param FromState     The current state tag.
     * @param ContextObject Optional context passed to UTransitionRule::Evaluate.
     * @return The ToState tag of the first passing transition, or an invalid tag if none pass.
     */
    FGameplayTag FindFirstPassingTransition(
        const FGameplayTag& FromState,
        UObject* ContextObject = nullptr) const;

    /**
     * Finds the state node for a given state tag. Returns nullptr if not found.
     */
    const UStateNodeBase* FindNode(const FGameplayTag& StateTag) const;

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("StateMachineAsset"), GetFName());
    }

private:
    // Transition source tag stored alongside each UTransitionRule.
    // Populated at asset load time for fast lookup.
    TMap<FGameplayTag, TArray<int32>> TransitionIndexByFromState;

    void RebuildTransitionIndex();
};
