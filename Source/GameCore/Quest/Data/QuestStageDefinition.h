#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Requirements/RequirementList.h"
#include "QuestStageDefinition.generated.h"

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestProgressTrackerDef
{
    GENERATED_BODY()

    /** Unique key for this tracker within the stage. Convention: Quest.Counter.<Domain>.<Target> */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(Categories="Quest.Counter"))
    FGameplayTag TrackerKey;

    /** Target value for solo play (GroupSize == 1). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin=1))
    int32 TargetValue = 1;

    /**
     * Per-additional-member contribution multiplier.
     * 0.0 = non-scalable. 0.5 = each extra member adds 50% of TargetValue.
     * EffectiveTarget = TargetValue + (GroupSize - 1) * TargetValue * ScalingMultiplier
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin=0.0f, ClampMax=1.0f))
    float ScalingMultiplier = 0.0f;

    /**
     * If true: no FQuestTrackerEntry is created. CompletionRequirements re-evaluate
     * from live world state on every check. Use for inventory, zone, or equipment conditions.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    bool bReEvaluateOnly = false;

    int32 GetEffectiveTarget(int32 GroupSize) const
    {
        if (GroupSize <= 1 || ScalingMultiplier <= 0.0f) return TargetValue;
        return FMath::RoundToInt(
            TargetValue + (GroupSize - 1) * TargetValue * ScalingMultiplier);
    }
};

UCLASS(EditInlineNew, CollapseCategories, BlueprintType)
class GAMECORE_API UQuestStageDefinition : public UObject
{
    GENERATED_BODY()
public:
    /** Must match a state tag in UQuestDefinition::StageGraph. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FGameplayTag StageTag;

    /** Requirements that must all pass for the stage to be considered complete. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    TObjectPtr<URequirementList> CompletionRequirements;

    /** Progress counters for this stage. bReEvaluateOnly entries have no persistent counter. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    TArray<FQuestProgressTrackerDef> Trackers;

    /** Localised objective text. Broadcast in FQuestStageChangedPayload. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FText StageObjectiveText;

    /** Entering this stage means the quest has been completed successfully. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    bool bIsCompletionState = false;

    /** Entering this stage means the quest has failed. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    bool bIsFailureState = false;

    const FQuestProgressTrackerDef* FindTrackerDef(const FGameplayTag& Key) const
    {
        return Trackers.FindByPredicate(
            [&](const FQuestProgressTrackerDef& T){ return T.TrackerKey == Key; });
    }

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const;
#endif
};
