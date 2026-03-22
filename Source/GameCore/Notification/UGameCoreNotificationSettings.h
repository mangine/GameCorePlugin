#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UNotificationChannelConfig.h"
#include "UNotificationCategoryConfig.h"
#include "UGameCoreNotificationSettings.generated.h"

/**
 * UGameCoreNotificationSettings
 *
 * UDeveloperSettings subclass. Appears under Project Settings → Game → Notification System.
 * Stores soft references to the two config assets. No runtime assignment required.
 *
 * The subsystem resolves both assets via LoadSynchronous() during Initialize.
 * Both may be null — the system degrades gracefully:
 *   - No ChannelConfig → no GMS subscriptions (direct PushNotification still works)
 *   - No CategoryConfig → unlimited stacking, no auto-view on any category
 *
 * Access pattern: GetDefault<UGameCoreNotificationSettings>()
 * Never store a pointer to the settings object — always call GetDefault.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Notification System"))
class GAMECORE_API UGameCoreNotificationSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UGameCoreNotificationSettings();

    virtual FName GetCategoryName() const override { return TEXT("Game"); }

    // Defines which Event Bus channels to listen to and how to convert them.
    // If null, only PushNotification() direct calls will produce notifications.
    UPROPERTY(Config, EditAnywhere, Category = "Configuration")
    TSoftObjectPtr<UNotificationChannelConfig> ChannelConfig;

    // Defines per-category stacking rules.
    // If null, all categories use unlimited stacking and no auto-view.
    UPROPERTY(Config, EditAnywhere, Category = "Configuration")
    TSoftObjectPtr<UNotificationCategoryConfig> CategoryConfig;
};
