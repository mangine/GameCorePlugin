# FNotificationEntry

The atomic notification unit. Created by `UNotificationChannelBinding::BuildEntry` or passed directly to `UGameCoreNotificationSubsystem::PushNotification`. Immutable after `PushEntryInternal` assigns the `Id` and `Timestamp`.

**File:** `Notification/FNotificationEntry.h`

---

## Declaration

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FNotificationEntry
{
    GENERATED_BODY()

    // Unique ephemeral ID. Invalid (default) before push.
    // Assigned by PushEntryInternal via FGuid::NewGuid().
    // Never stable across sessions.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FGuid Id;

    // Category tag. Must be valid — entries with invalid CategoryTag are suppressed.
    // Must match a tag registered in UNotificationCategoryConfig,
    // or an unconfigured tag uses default stacking rules (unlimited, no auto-view).
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FGameplayTag CategoryTag;

    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FText Title;

    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FText Body;

    // Optional icon. May be null — UI handles gracefully.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    TObjectPtr<UTexture2D> Icon = nullptr;

    // Seconds until auto-dismiss. <= 0 means no expiry.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    float ExpirySeconds = 0.f;

    // Set to false at push time. Set to true by MarkViewed.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    bool bViewed = false;

    // UTC wall-clock timestamp of when this entry was pushed.
    // Set by PushEntryInternal — bindings should not set this.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FDateTime Timestamp;

    // Arbitrary key-value payload for game-layer use (e.g. ItemId, QuestTag).
    // GameCore never reads this — it is passed through to UI via delegates.
    // Use to avoid versioning FNotificationEntry with game-specific fields.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    TMap<FName, FString> Metadata;
};
```

---

## Notes

- `Id` and `Timestamp` must be left at default when constructing an entry in a binding or in game code. `PushEntryInternal` overwrites them — any pre-set value is discarded.
- `CategoryTag.IsValid() == false` is the suppression signal. `HandleIncomingEntry` discards entries with an invalid category tag silently.
- `Metadata` is an intentional escape hatch. Bindings pack arbitrary context here (e.g. `{"NewLevel", "5"}`) so UI widgets can display game-specific details without a custom entry type.
- `Icon` is a hard reference (`TObjectPtr`). Bindings should use icons that are already loaded (e.g. from a pre-loaded data asset). Loading a texture for every notification on the fly is not recommended.
