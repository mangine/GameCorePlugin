#include "UNotificationChannelBinding.h"
#include "UGameCoreNotificationSubsystem.h"
#include "EventBus/GameCoreEventBus.h"
#include "StructUtils/InstancedStruct.h"

FNotificationEntry UNotificationChannelBinding::BuildEntry_Implementation(const FGameplayTag& InChannel) const
{
    // Default returns a suppressed (invalid CategoryTag) entry.
    // Subclasses must override this to produce a valid notification.
    return FNotificationEntry{};
}

void UNotificationChannelBinding::RegisterListener(
    UGameCoreEventBus*                     Bus,
    UGameCoreNotificationSubsystem*        Subsystem,
    TArray<FGameplayMessageListenerHandle>& OutHandles)
{
    if (!Bus || !Channel.IsValid()) return;

    // Default: untyped FInstancedStruct listener.
    // Blueprint bindings pull game state from already-replicated objects in BuildEntry.
    // The payload is intentionally ignored here.
    FGameplayMessageListenerHandle Handle = Bus->StartListening(
        Channel,
        [this, Subsystem](FGameplayTag InChannel, const FInstancedStruct& /*Payload*/)
        {
            FNotificationEntry Entry = BuildEntry(InChannel);
            if (!Entry.CategoryTag.IsValid()) return; // Binding suppressed
            Subsystem->HandleIncomingEntry(MoveTemp(Entry));
        });

    OutHandles.Add(Handle);
}

void UNotificationChannelBinding::UnregisterListeners(
    UGameCoreEventBus*                     Bus,
    TArray<FGameplayMessageListenerHandle>& Handles)
{
    if (!Bus) return;
    for (FGameplayMessageListenerHandle& Handle : Handles)
        Bus->StopListening(Handle);
    Handles.Empty();
}
