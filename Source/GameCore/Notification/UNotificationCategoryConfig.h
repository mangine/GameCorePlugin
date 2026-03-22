#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FNotificationCategoryRule.h"
#include "UNotificationCategoryConfig.generated.h"

/**
 * UNotificationCategoryConfig
 *
 * UDataAsset. Stores per-category stacking rules. Assigned in Project Settings
 * via UGameCoreNotificationSettings::CategoryConfig.
 * Loaded synchronously at UGameCoreNotificationSubsystem::Initialize.
 *
 * FindRule performs a linear search — expected < 50 categories.
 * Duplicate CategoryTag values are a configuration error; the first matching rule wins.
 */
UCLASS(BlueprintType)
class GAMECORE_API UNotificationCategoryConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    // One rule per category tag. CategoryTag must be unique within this array.
    // Categories without a matching rule use the default rule (unlimited, no auto-view).
    UPROPERTY(EditDefaultsOnly, Category = "Categories")
    TArray<FNotificationCategoryRule> Rules;

    // Returns the rule for the given tag.
    // Returns nullptr if no rule exists — caller uses the static default rule.
    const FNotificationCategoryRule* FindRule(FGameplayTag CategoryTag) const;
};
