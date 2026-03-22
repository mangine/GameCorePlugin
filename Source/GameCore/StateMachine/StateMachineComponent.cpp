#include "StateMachine/StateMachineComponent.h"
#include "StateMachine/StateMachineAsset.h"
#include "StateMachine/StateMachineTypes.h"

void UStateMachineComponent::BeginPlay()
{
    Super::BeginPlay();
    if (Asset && Asset->EntryStateTag.IsValid())
        EnterState(Asset->EntryStateTag);
}

bool UStateMachineComponent::TryTransition(UObject* ContextObject)
{
    if (!Asset) return false;
    const FGameplayTag Next = Asset->FindFirstPassingTransition(CurrentStateTag, ContextObject);
    if (!Next.IsValid()) return false;
    ExitState(CurrentStateTag);
    EnterState(Next);
    return true;
}

void UStateMachineComponent::ForceState(FGameplayTag NewStateTag)
{
    ExitState(CurrentStateTag);
    EnterState(NewStateTag);
}

void UStateMachineComponent::EnterState(const FGameplayTag& NewStateTag)
{
    CurrentStateTag = NewStateTag;
    if (!Asset) return;
    UStateNodeBase* Node = const_cast<UStateNodeBase*>(Asset->FindNode(NewStateTag));
    if (Node) Node->OnEnter(this);
}

void UStateMachineComponent::ExitState(const FGameplayTag& OldStateTag)
{
    if (!Asset || !OldStateTag.IsValid()) return;
    UStateNodeBase* Node = const_cast<UStateNodeBase*>(Asset->FindNode(OldStateTag));
    if (Node) Node->OnExit(this);
}
