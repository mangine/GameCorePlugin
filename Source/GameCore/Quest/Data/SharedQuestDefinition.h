#pragma once

#include "CoreMinimal.h"
#include "Quest/Data/QuestDefinition.h"
#include "Quest/Enums/QuestEnums.h"
#include "SharedQuestDefinition.generated.h"

/**
 * Extends UQuestDefinition with group enrollment configuration.
 * When played solo (no IGroupProvider bound), behaves identically to UQuestDefinition.
 * ScalingMultiplier returns TargetValue unchanged for GroupSize == 1.
 */
UCLASS(BlueprintType)
class GAMECORE_API USharedQuestDefinition : public UQuestDefinition
{
    GENERATED_BODY()
public:

    /** How the group collectively accepts this quest. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group")
    ESharedQuestAcceptance AcceptanceMode = ESharedQuestAcceptance::IndividualAccept;

    /**
     * Grace window passed to OnRequestGroupEnrollment on the coordinator.
     * Only meaningful for LeaderAccept. The group system owns this timer.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group",
              meta=(EditCondition="AcceptanceMode == ESharedQuestAcceptance::LeaderAccept",
                    ClampMin=0.0f))
    float LeaderAcceptGraceSeconds = 10.0f;

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        // Same asset type as base — registry loads both identically.
        return FPrimaryAssetId(TEXT("QuestDefinition"), GetFName());
    }
};
