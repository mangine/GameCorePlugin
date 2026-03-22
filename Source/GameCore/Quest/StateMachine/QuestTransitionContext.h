#pragma once

#include "CoreMinimal.h"
#include "Requirements/RequirementContext.h"
#include "QuestTransitionContext.generated.h"

/**
 * Thin UObject wrapper for FRequirementContext, used as the ContextObject parameter in
 * UTransitionRule::Evaluate when called from UQuestComponent::ResolveNextStage.
 *
 * UTransitionRule::Evaluate takes UObject* but FRequirementContext is a plain struct.
 * This wrapper provides type-safe casting in UQuestTransitionRule::Evaluate.
 * Created per-evaluation via NewObject<UQuestTransitionContext>(GetTransientPackage())
 * and GC'd immediately after FindFirstPassingTransition returns.
 */
UCLASS()
class GAMECORE_API UQuestTransitionContext : public UObject
{
    GENERATED_BODY()
public:
    FRequirementContext Context;
};
