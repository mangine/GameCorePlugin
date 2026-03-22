#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "GameplayTagContainer.h"
#include "QuestMarkerDataAsset.generated.h"

/**
 * Maps quest marker tags to icons.
 * Loaded by the UI — never by the quest system.
 */
UCLASS(BlueprintType)
class GAMECORE_API UQuestMarkerDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Markers")
    TMap<FGameplayTag, TSoftObjectPtr<UTexture2D>> MarkerIcons;

    TSoftObjectPtr<UTexture2D> GetIcon(const FGameplayTag& MarkerTag) const
    {
        const TSoftObjectPtr<UTexture2D>* Found = MarkerIcons.Find(MarkerTag);
        return Found ? *Found : TSoftObjectPtr<UTexture2D>();
    }
};
