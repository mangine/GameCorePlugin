#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FNotificationEntry.h"
#include "FNotificationGroup.generated.h"

/**
 * FNotificationGroup
 *
 * All FNotificationEntry values sharing the same CategoryTag.
 * Maintained by UGameCoreNotificationSubsystem::Groups.
 * Passed by const-ref in the OnGroupChanged delegate.
 *
 * UnviewedCount is a write-through counter maintained exclusively by the subsystem.
 * Never modify it externally.
 *
 * Entries are ordered oldest-first. The newest entry is always Last().
 * A group is removed from the subsystem when its last entry is dismissed.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FNotificationGroup
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Notification")
    FGameplayTag CategoryTag;

    // All active entries for this category, ordered oldest-first.
    // Newest entry is always Last().
    UPROPERTY(BlueprintReadOnly, Category = "Notification")
    TArray<FNotificationEntry> Entries;

    // Count of entries where bViewed == false.
    // Maintained as a write-through counter by the subsystem.
    UPROPERTY(BlueprintReadOnly, Category = "Notification")
    int32 UnviewedCount = 0;

    // Returns how many entries the UI should display given the configured max.
    // MaxStackCount == 0 means unlimited — returns Entries.Num().
    int32 GetDisplayCount(int32 MaxStackCount) const
    {
        if (MaxStackCount <= 0) return Entries.Num();
        return FMath::Min(Entries.Num(), MaxStackCount);
    }
};
