#pragma once

#include "CoreMinimal.h"
#include "Requirements/Requirement.h"
#include "Quest/Runtime/QuestRuntime.h"
#include "Requirement_ActiveQuestCount.generated.h"

/**
 * Passes when the player's active quest count is below MaxAllowed.
 */
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Active Quest Capacity"))
class GAMECORE_API URequirement_ActiveQuestCount : public URequirement
{
    GENERATED_BODY()
public:

    UPROPERTY(EditDefaultsOnly, Category="Requirement", meta=(ClampMin=1))
    int32 MaxAllowed = 20;

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        const FQuestEvaluationContext* CtxData =
            Context.Data.GetPtr<FQuestEvaluationContext>();
        if (!CtxData || !CtxData->PlayerState)
            return FRequirementResult::Pass();

        const UQuestComponent* QC =
            CtxData->PlayerState->FindComponentByClass<UQuestComponent>();
        if (!QC) return FRequirementResult::Pass();

        return QC->ActiveQuests.Items.Num() < MaxAllowed
            ? FRequirementResult::Pass()
            : FRequirementResult::Fail(
                NSLOCTEXT("QuestRequirements", "AtCap", "Quest log is full."));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Active Quests < %d"), MaxAllowed);
    }
#endif
};
