#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "StateMachineComponent.generated.h"

class UStateMachineAsset;
class UStateNodeBase;

/**
 * Runtime state machine component. Drives UStateNodeBase::OnEnter/OnExit
 * and manages active state transitions.
 *
 * Note: The Quest System does NOT use this component — it drives transitions
 * directly via UStateMachineAsset::FindFirstPassingTransition.
 */
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UStateMachineComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** The state machine asset defining the graph. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="StateMachine")
    TObjectPtr<UStateMachineAsset> Asset;

    /** Current active state tag. */
    UPROPERTY(BlueprintReadOnly, Category="StateMachine")
    FGameplayTag CurrentStateTag;

    virtual void BeginPlay() override;

    /** Attempt to fire a transition from the current state using the given context. */
    UFUNCTION(BlueprintCallable, Category="StateMachine")
    bool TryTransition(UObject* ContextObject = nullptr);

    /** Force a specific state (skips transition rules). */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="StateMachine")
    void ForceState(FGameplayTag NewStateTag);

private:
    void EnterState(const FGameplayTag& NewStateTag);
    void ExitState(const FGameplayTag& OldStateTag);
};
