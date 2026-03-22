#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UNotificationChannelBinding.h"
#include "UNotificationChannelConfig.generated.h"

/**
 * UNotificationChannelConfig
 *
 * UDataAsset. Stores the list of UNotificationChannelBinding instances,
 * each describing one Event Bus channel. Assigned in Project Settings.
 * Loaded synchronously at UGameCoreNotificationSubsystem::Initialize.
 *
 * EditInlineNew + DefaultToInstanced on UNotificationChannelBinding allow
 * authoring binding instances directly in the Details panel.
 *
 * Two bindings with the same Channel tag will produce duplicate notifications.
 * Null entries in Bindings are silently skipped by RegisterChannelListeners.
 */
UCLASS(BlueprintType)
class GAMECORE_API UNotificationChannelConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    // Inline-instanced bindings. Each binding owns its Channel tag and BuildEntry logic.
    UPROPERTY(EditDefaultsOnly, Instanced, Category = "Channels")
    TArray<TObjectPtr<UNotificationChannelBinding>> Bindings;
};
