#pragma once

#include "CoreMinimal.h"
#include "Requirements/Requirement.h"
#include "Quest/Runtime/QuestRuntime.h"
#include "Quest/Events/QuestEventPayloads.h"
#include "Requirement_QuestCompleted.generated.h"

class UQuestComponent;

/**
 * Passes when the player has the specified QuestCompletedTag in their CompletedQuestTags.
 * bIsMonotonic = true — completion is permanent for singleton quests.
 */
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Completed"))
class GAMECORE_API URequirement_QuestCompleted : public URequirement
{
    GENERATED_BODY()
public:

    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Completed"))
    FGameplayTag RequiredQuestCompletedTag;

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override;

    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(TAG_RequirementEvent_Quest_Completed);
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Quest Completed: %s"),
            *RequiredQuestCompletedTag.ToString());
    }
#endif
};
