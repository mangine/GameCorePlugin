# UNotificationCategoryConfig

`UDataAsset`. Stores per-category stacking rules. Assigned in Project Settings via `UGameCoreNotificationSettings::CategoryConfig`. Loaded synchronously at subsystem `Initialize`.

**File:** `Notification/UNotificationCategoryConfig.h`

---

## Declaration

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UNotificationCategoryConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    // One rule per category tag. CategoryTag must be unique within this array.
    // Categories without a matching rule use the default rule (unlimited, no auto-view).
    UPROPERTY(EditDefaultsOnly, Category="Categories")
    TArray<FNotificationCategoryRule> Rules;

    // Returns the rule for the given tag.
    // Returns nullptr if no rule exists — caller uses the static default rule.
    const FNotificationCategoryRule* FindRule(FGameplayTag CategoryTag) const;
};
```

---

## Implementation

```cpp
const FNotificationCategoryRule* UNotificationCategoryConfig::FindRule(
    FGameplayTag CategoryTag) const
{
    for (const FNotificationCategoryRule& Rule : Rules)
    {
        if (Rule.CategoryTag == CategoryTag)
            return &Rule;
    }
    return nullptr; // Caller falls back to static default.
}
```

---

## Notes

- `FindRule` performs a linear search. The number of category rules is expected to be small (< 50). If this becomes a bottleneck, a `TMap<FGameplayTag, int32>` index can be built in `PostLoad`.
- Duplicate `CategoryTag` values across `Rules` entries are a configuration error. The first matching rule wins. `IsDataValid` should check for duplicates but this is not currently implemented — see Code Review.
