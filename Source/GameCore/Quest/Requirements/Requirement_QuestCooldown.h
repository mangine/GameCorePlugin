#pragma once

#include "CoreMinimal.h"
#include "Requirements/Requirement.h"
#include "Quest/Enums/QuestEnums.h"
#include "Quest/Runtime/QuestRuntime.h"
#include "Quest/Events/QuestEventPayloads.h"
#include "Requirement_QuestCooldown.generated.h"

/**
 * Passes when the quest cooldown has expired.
 * Reads LastCompletedTimestamp from UQuestComponent via FindComponentByClass.
 */
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Cooldown"))
class GAMECORE_API URequirement_QuestCooldown : public URequirement
{
    GENERATED_BODY()
public:

    /** Must match the QuestId this cooldown gates. */
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Id"))
    FGameplayTag QuestIdKey;

    UPROPERTY(EditDefaultsOnly, Category="Requirement")
    EQuestResetCadence Cadence = EQuestResetCadence::None;

    /** Only used when Cadence == None. */
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(EditCondition="Cadence == EQuestResetCadence::None",
                    ClampMin=0.0f))
    float CooldownSeconds = 86400.0f;

    virtual ERequirementDataAuthority GetDataAuthority() const override
    { return ERequirementDataAuthority::Both; }

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
        if (Cadence == EQuestResetCadence::None)
            return FString::Printf(TEXT("Cooldown: %.0fs"), CooldownSeconds);
        return FString::Printf(TEXT("Cadence: %s"),
            *UEnum::GetDisplayValueAsText(Cadence).ToString());
    }
#endif
};
