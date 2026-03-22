#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "GameplayMessageSubsystem.h"
#include "FNotificationEntry.h"
#include "UNotificationChannelBinding.generated.h"

class UGameCoreEventBus;
class UGameCoreNotificationSubsystem;

/**
 * UNotificationChannelBinding
 *
 * Abstract adapter. One subclass per Event Bus channel type. Blueprint-subclassable.
 * Owned inline by UNotificationChannelConfig.
 *
 * The binding is the only place game-specific message structs are referenced —
 * GameCore never knows about them.
 *
 * Default RegisterListener registers an untyped FInstancedStruct listener,
 * sufficient for Blueprint bindings that pull state from already-replicated objects
 * inside BuildEntry. C++ typed bindings override RegisterListener to capture the
 * typed payload before calling BuildEntry.
 *
 * Suppression contract: return an FNotificationEntry with CategoryTag.IsValid() == false
 * to discard the notification silently.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UNotificationChannelBinding : public UObject
{
    GENERATED_BODY()

public:
    // The Event Bus channel this binding listens to.
    // Must be unique across all bindings in UNotificationChannelConfig.
    UPROPERTY(EditDefaultsOnly, Category = "Binding")
    FGameplayTag Channel;

    // Override in C++ or Blueprint to convert the channel event into a notification entry.
    // Return an FNotificationEntry with CategoryTag.IsValid() == false to suppress.
    // Do NOT set Entry.Id or Entry.Timestamp — the subsystem assigns these.
    UFUNCTION(BlueprintNativeEvent, Category = "Notification")
    FNotificationEntry BuildEntry(const FGameplayTag& InChannel) const;
    virtual FNotificationEntry BuildEntry_Implementation(const FGameplayTag& InChannel) const;

    // Called by UGameCoreNotificationSubsystem::RegisterChannelListeners.
    // Default implementation registers an untyped FInstancedStruct listener —
    // sufficient for Blueprint bindings that pull data from replicated state in BuildEntry.
    // Override in C++ subclasses to register a typed listener that captures the payload
    // before calling BuildEntry.
    virtual void RegisterListener(
        UGameCoreEventBus*                     Bus,
        UGameCoreNotificationSubsystem*        Subsystem,
        TArray<FGameplayMessageListenerHandle>& OutHandles);

    // Called by UGameCoreNotificationSubsystem::UnregisterChannelListeners.
    // Default implementation removes all handles in OutHandles.
    // Override only if the binding registers listeners outside of RegisterListener.
    virtual void UnregisterListeners(
        UGameCoreEventBus*                     Bus,
        TArray<FGameplayMessageListenerHandle>& Handles);
};
