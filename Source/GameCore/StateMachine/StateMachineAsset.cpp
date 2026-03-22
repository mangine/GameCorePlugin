#include "StateMachine/StateMachineAsset.h"

FGameplayTag UStateMachineAsset::FindFirstPassingTransition(
    const FGameplayTag& FromState,
    UObject* ContextObject) const
{
    for (const TObjectPtr<UTransitionRule>& Rule : Transitions)
    {
        if (!Rule) continue;
        if (Rule->FromState != FromState) continue;
        if (Rule->Evaluate(nullptr, ContextObject))
            return Rule->ToState;
    }
    return FGameplayTag{};
}

const UStateNodeBase* UStateMachineAsset::FindNode(const FGameplayTag& StateTag) const
{
    for (const TObjectPtr<UStateNodeBase>& Node : Nodes)
    {
        if (Node && Node->StateTag == StateTag)
            return Node.Get();
    }
    return nullptr;
}
