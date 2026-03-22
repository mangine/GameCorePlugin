#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "QuestConfigDataAsset.generated.h"

/**
 * Externalizes tunables from UQuestComponent.
 * Assign to UQuestComponent::QuestConfig to avoid recompiling for tuning changes.
 */
UCLASS(BlueprintType)
class GAMECORE_API UQuestConfigDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    /** Server-enforced. Client reads ActiveQuests.Items.Num() for pre-validation UI hints. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(ClampMin=1, ClampMax=200))
    int32 MaxActiveQuests = 20;
};
