#include "UNotificationCategoryConfig.h"

const FNotificationCategoryRule* UNotificationCategoryConfig::FindRule(FGameplayTag CategoryTag) const
{
    for (const FNotificationCategoryRule& Rule : Rules)
    {
        if (Rule.CategoryTag == CategoryTag)
            return &Rule;
    }
    return nullptr; // Caller falls back to static default.
}
