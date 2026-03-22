#pragma once

#include "CoreMinimal.h"
#include "Quest/Enums/QuestEnums.h"
#include "Engine/Texture2D.h"
#include "QuestDisplayData.generated.h"

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestDisplayData
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    FText Title;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    FText ShortDescription;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    FText LongDescription;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    EQuestDifficulty Difficulty = EQuestDifficulty::Normal;

    /** Soft reference — loaded by UI on demand, never by the quest system. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    TSoftObjectPtr<UTexture2D> QuestImage;
};
