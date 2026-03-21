# FNotificationGroup

All `FNotificationEntry` values sharing the same `CategoryTag`. Maintained by `UGameCoreNotificationSubsystem::Groups`. Passed by const-ref in `OnGroupChanged` delegate.

**File:** `Notification/FNotificationGroup.h`

---

## Declaration

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FNotificationGroup
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FGameplayTag CategoryTag;

    // All active entries for this category, ordered oldest-first.
    // Newest entry is always Last().
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    TArray<FNotificationEntry> Entries;

    // Count of entries where bViewed == false.
    // Maintained as a write-through counter by the subsystem.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    int32 UnviewedCount = 0;

    // Returns how many entries the UI should display given the configured max.
    // MaxStackCount == 0 means unlimited — returns Entries.Num().
    int32 GetDisplayCount(int32 MaxStackCount) const
    {
        if (MaxStackCount <= 0) return Entries.Num();
        return FMath::Min(Entries.Num(), MaxStackCount);
    }
};
```

---

## Notes

- `UnviewedCount` is maintained by the subsystem as a write-through counter. It must never be modified externally. Any desync between `UnviewedCount` and the actual count of entries with `bViewed == false` is a subsystem bug.
- `Entries` is ordered oldest-first. The eviction strategy (`MaxStackCount`) removes from index 0 (oldest). When rendering a "latest notification" badge, use `Entries.Last()`.
- A group is removed from `UGameCoreNotificationSubsystem::Groups` when its last entry is dismissed. `GetGroup()` returns a default-constructed (empty) group if no group exists for a given tag.
