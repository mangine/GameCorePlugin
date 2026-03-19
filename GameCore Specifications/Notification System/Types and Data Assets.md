# Types & Data Assets

**Sub-page of:** [Notification System Overview](Notification%20System%20Overview.md)

---

## `FNotificationEntry`

The atomic unit. Created by `UNotificationChannelBinding::BuildEntry` or passed directly to `PushNotification`.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FNotificationEntry
{
    GENERATED_BODY()

    // Unique ephemeral ID. Generated at push time via FGuid::NewGuid().
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FGuid Id;

    // Category tag. Must match a tag registered in UNotificationCategoryConfig
    // (or an unconfigured tag will use default stacking rules: unlimited, no auto-view).
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FGameplayTag CategoryTag;

    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FText Title;

    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FText Body;

    // Optional icon. May be null — UI should handle gracefully.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    TObjectPtr<UTexture2D> Icon = nullptr;

    // Seconds until auto-dismiss. <= 0 means no expiry.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    float ExpirySeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category="Notification")
    bool bViewed = false;

    // Wall-clock timestamp of when this entry was pushed.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FDateTime Timestamp;

    // Arbitrary key-value payload for game-layer use (e.g. ItemId, QuestTag).
    // GameCore never reads this — it is passed through to UI via the delegate.
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    TMap<FName, FString> Metadata;
};
```

> **`Metadata`** is a deliberate escape hatch. Rather than versioning `FNotificationEntry` with game-specific fields, the binding can pack arbitrary context here. The UI reads whatever keys it needs.

---

## `FNotificationGroup`

All entries sharing the same `CategoryTag`. Maintained by the subsystem.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FNotificationGroup
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Notification")
    FGameplayTag CategoryTag;

    // Ordered oldest-first. Newest entry is Last().
    UPROPERTY(BlueprintReadOnly, Category="Notification")
    TArray<FNotificationEntry> Entries;

    UPROPERTY(BlueprintReadOnly, Category="Notification")
    int32 UnviewedCount = 0;

    // Returns min(Entries.Num(), MaxStackCount) where MaxStackCount comes
    // from UNotificationCategoryConfig. 0 MaxStackCount means return Entries.Num().
    // Used by UI to know how many stack badges to render.
    int32 GetDisplayCount(int32 MaxStackCount) const
    {
        if (MaxStackCount <= 0) return Entries.Num();
        return FMath::Min(Entries.Num(), MaxStackCount);
    }
};
```

---

## `FNotificationCategoryRule`

Per-category stacking and display rules. Lives inside `UNotificationCategoryConfig`.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FNotificationCategoryRule
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, Category="Category")
    FGameplayTag CategoryTag;

    // Maximum entries kept in the group. When exceeded, oldest entry is evicted (FIFO).
    // 0 = unlimited.
    UPROPERTY(EditDefaultsOnly, Category="Category")
    int32 MaxStackCount = 0;

    // When set, the UI should display this format string instead of the individual title
    // when the group has more than 1 entry. Use {Count} token for the number.
    // Example: "{Count} new quests available"
    UPROPERTY(EditDefaultsOnly, Category="Category")
    FText StackedTitleFormat;

    // If true, pushing a new entry into this group marks all previous entries as viewed.
    // Useful for categories where only the latest matters (e.g. combat alerts).
    UPROPERTY(EditDefaultsOnly, Category="Category")
    bool bAutoViewOnStack = false;
};
```

---

## `UNotificationCategoryConfig`

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UNotificationCategoryConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Categories")
    TArray<FNotificationCategoryRule> Rules;

    // Returns the rule for the given tag, or a default-constructed rule
    // (MaxStackCount=0, no format, bAutoViewOnStack=false) if not found.
    // Never returns null — callers always get a valid rule.
    const FNotificationCategoryRule* FindRule(FGameplayTag CategoryTag) const;
};

// Implementation:
const FNotificationCategoryRule* UNotificationCategoryConfig::FindRule(FGameplayTag CategoryTag) const
{
    for (const FNotificationCategoryRule& Rule : Rules)
    {
        if (Rule.CategoryTag == CategoryTag)
            return &Rule;
    }
    return nullptr; // Caller uses default rule
}
```

---

## `UNotificationChannelBinding`

Abstract adapter. One subclass per GMS channel type. Blueprint-subclassable.

```cpp
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UNotificationChannelBinding : public UObject
{
    GENERATED_BODY()
public:
    // The GMS channel this binding listens to.
    UPROPERTY(EditDefaultsOnly, Category="Binding")
    FGameplayTag Channel;

    // Override in C++ or Blueprint to convert the raw GMS payload into a notification entry.
    // The subsystem calls this when a message arrives on Channel.
    // Return a default FNotificationEntry with an invalid Id to suppress (Id.IsValid() == false
    // means "do not push").
    UFUNCTION(BlueprintNativeEvent, Category="Notification")
    FNotificationEntry BuildEntry(const FGameplayTag& InChannel) const;
    virtual FNotificationEntry BuildEntry_Implementation(const FGameplayTag& InChannel) const;
};
```

> **Why no payload parameter on `BuildEntry`?** GMS is templated in C++ but untyped at the Blueprint level. Passing the raw struct would require `FInstancedStruct` or a wrapper — adding complexity. Instead, C++ subclasses receive their typed payload via a constructor-injected reference captured before `BuildEntry` is called (see [GMS Integration](GMS%20Integration.md) for the pattern). Blueprint subclasses query game state themselves (e.g. read from a replicated component on the local player) since the GMS event is the trigger, not the sole data source.

---

## `UNotificationChannelConfig`

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UNotificationChannelConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    // Each binding declares its own Channel tag and BuildEntry logic.
    // Use EditInlineNew + DefaultToInstanced so bindings are authored inline in the asset.
    UPROPERTY(EditDefaultsOnly, Instanced, Category="Channels")
    TArray<TObjectPtr<UNotificationChannelBinding>> Bindings;
};
```

---

## `UGameCoreNotificationSettings`

Project-level settings. Appears under **Project Settings → Game → Notification** in the editor.

```cpp
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Notification System"))
class GAMECORE_API UGameCoreNotificationSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UGameCoreNotificationSettings();

    virtual FName GetCategoryName() const override { return TEXT("Game"); }

    // Defines which GMS channels to listen to and how to convert them.
    // Must be assigned for GMS-sourced notifications to work.
    // If null, only PushNotification() direct calls will work.
    UPROPERTY(Config, EditAnywhere, Category="Configuration")
    TSoftObjectPtr<UNotificationChannelConfig> ChannelConfig;

    // Defines per-category stacking rules.
    // If null, all categories use unlimited stacking and no auto-view.
    UPROPERTY(Config, EditAnywhere, Category="Configuration")
    TSoftObjectPtr<UNotificationCategoryConfig> CategoryConfig;
};
```

> Assets are stored as `TSoftObjectPtr` in project settings to avoid hard-loading at startup. The subsystem resolves them synchronously at `Initialize` via `LoadSynchronous()` — acceptable since `Initialize` runs during world setup, not on the game thread tick.
