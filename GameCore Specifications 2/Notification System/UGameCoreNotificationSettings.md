# UGameCoreNotificationSettings

`UDeveloperSettings` subclass. Appears under **Project Settings → Game → Notification System**. Stores soft references to the two config assets. No runtime assignment required.

**File:** `Notification/UGameCoreNotificationSettings.h`

---

## Declaration

```cpp
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Notification System"))
class GAMECORE_API UGameCoreNotificationSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UGameCoreNotificationSettings();

    virtual FName GetCategoryName() const override { return TEXT("Game"); }

    // Defines which Event Bus channels to listen to and how to convert them.
    // If null, only PushNotification() direct calls will produce notifications.
    UPROPERTY(Config, EditAnywhere, Category="Configuration")
    TSoftObjectPtr<UNotificationChannelConfig> ChannelConfig;

    // Defines per-category stacking rules.
    // If null, all categories use unlimited stacking and no auto-view.
    UPROPERTY(Config, EditAnywhere, Category="Configuration")
    TSoftObjectPtr<UNotificationCategoryConfig> CategoryConfig;
};
```

---

## Notes

- Assets are stored as `TSoftObjectPtr` to avoid hard-loading at editor startup. The subsystem resolves them via `LoadSynchronous()` during `Initialize`, which runs during world setup — acceptable because this is not on the game thread tick.
- `GetDefault<UGameCoreNotificationSettings>()` is the access pattern. Never store a pointer to the settings object — always call `GetDefault` to get the CDO.
- Both `ChannelConfig` and `CategoryConfig` may be null. The system degrades gracefully: no GMS subscriptions without `ChannelConfig`, unlimited stacking without `CategoryConfig`.
