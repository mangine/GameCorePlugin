#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FNotificationCategoryRule.generated.h"

/**
 * FNotificationCategoryRule
 *
 * Per-category stacking and display rules. Stored in UNotificationCategoryConfig::Rules.
 * Retrieved at push time by UGameCoreNotificationSubsystem::GetRule.
 *
 * Default rule (used for unconfigured categories):
 *   MaxStackCount = 0  — unlimited entries
 *   StackedTitleFormat — empty (UI shows the latest entry title)
 *   bAutoViewOnStack   = false
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FNotificationCategoryRule
{
    GENERATED_BODY()

    // The category this rule applies to. Must be unique across all Rules in UNotificationCategoryConfig.
    UPROPERTY(EditDefaultsOnly, Category = "Category")
    FGameplayTag CategoryTag;

    // Maximum entries kept in the group. When exceeded, oldest entry is evicted (FIFO).
    // 0 = unlimited.
    UPROPERTY(EditDefaultsOnly, Category = "Category")
    int32 MaxStackCount = 0;

    // When set, the UI should display this format string when the group has more than 1 entry.
    // Use {Count} token for the number. Example: "{Count} new quests available".
    // When empty, the UI shows the most recent entry's title.
    UPROPERTY(EditDefaultsOnly, Category = "Category")
    FText StackedTitleFormat;

    // If true, pushing a new entry marks all existing entries in the group as viewed before adding.
    // Useful for categories where only the latest matters (e.g. combat alerts).
    UPROPERTY(EditDefaultsOnly, Category = "Category")
    bool bAutoViewOnStack = false;
};
