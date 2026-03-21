# UNotificationChannelBinding

Abstract adapter. One subclass per Event Bus channel type. Blueprint-subclassable. Owned inline by `UNotificationChannelConfig`. The binding is the only place game-specific message structs are referenced — GameCore never knows about them.

**File:** `Notification/UNotificationChannelBinding.h`

---

## Declaration

```cpp
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UNotificationChannelBinding : public UObject
{
    GENERATED_BODY()
public:
    // The Event Bus channel this binding listens to.
    // Must be unique across all bindings in UNotificationChannelConfig.
    UPROPERTY(EditDefaultsOnly, Category="Binding")
    FGameplayTag Channel;

    // Override in C++ or Blueprint to convert the channel event into a notification entry.
    // Return an FNotificationEntry with CategoryTag.IsValid() == false to suppress.
    // Do NOT set Entry.Id or Entry.Timestamp — the subsystem assigns these.
    UFUNCTION(BlueprintNativeEvent, Category="Notification")
    FNotificationEntry BuildEntry(const FGameplayTag& InChannel) const;
    virtual FNotificationEntry BuildEntry_Implementation(const FGameplayTag& InChannel) const;

    // Called by UGameCoreNotificationSubsystem::RegisterChannelListeners.
    // Override in C++ subclasses to register a typed listener that captures the payload
    // before calling BuildEntry.
    // Default implementation registers an untyped FInstancedStruct listener —
    // sufficient for Blueprint bindings that pull data from replicated state in BuildEntry.
    virtual void RegisterListener(
        UGameCoreEventSubsystem*         Bus,
        UGameCoreNotificationSubsystem*  Subsystem,
        TArray<FGameplayMessageListenerHandle>& OutHandles);

    // Called by UGameCoreNotificationSubsystem::UnregisterChannelListeners.
    // Default implementation removes all handles in OutHandles.
    // Override only if the binding registers listeners outside of RegisterListener.
    virtual void UnregisterListeners(
        UGameCoreEventSubsystem*               Bus,
        TArray<FGameplayMessageListenerHandle>& Handles);
};
```

---

## Default `RegisterListener` Implementation

Used by Blueprint bindings. The payload is ignored — the binding pulls game state from already-replicated objects inside `BuildEntry_Implementation`.

```cpp
void UNotificationChannelBinding::RegisterListener(
    UGameCoreEventSubsystem*               Bus,
    UGameCoreNotificationSubsystem*        Subsystem,
    TArray<FGameplayMessageListenerHandle>& OutHandles)
{
    if (!Bus || !Channel.IsValid()) return;

    FGameplayMessageListenerHandle Handle = Bus->StartListening(
        Channel,
        EGameCoreEventScope::ClientOnly,
        this,
        [this, Subsystem](FGameplayTag InChannel, const FInstancedStruct& /*Payload*/)
        {
            FNotificationEntry Entry = BuildEntry(InChannel);
            if (!Entry.CategoryTag.IsValid()) return; // Binding suppressed
            Subsystem->HandleIncomingEntry(MoveTemp(Entry));
        });

    OutHandles.Add(Handle);
}
```

## Default `UnregisterListeners` Implementation

```cpp
void UNotificationChannelBinding::UnregisterListeners(
    UGameCoreEventSubsystem*               Bus,
    TArray<FGameplayMessageListenerHandle>& Handles)
{
    if (!Bus) return;
    for (FGameplayMessageListenerHandle& Handle : Handles)
        Bus->StopListening(Handle);
    Handles.Empty();
}
```

---

## C++ Typed Override Pattern

C++ bindings capture the typed payload before `BuildEntry` reads it:

```cpp
// In game module — NOT in GameCore.
UCLASS()
class ULevelUpNotificationBinding : public UNotificationChannelBinding
{
    GENERATED_BODY()
public:
    virtual FNotificationEntry BuildEntry_Implementation(
        const FGameplayTag& InChannel) const override
    {
        FNotificationEntry Entry;
        Entry.CategoryTag   = FGameplayTag::RequestGameplayTag("Notification.Category.Progression");
        Entry.Title         = FText::Format(
            LOCTEXT("LevelUp", "Level {0}!"), FText::AsNumber(LastMessage.NewLevel));
        Entry.Body          = LOCTEXT("LevelUpBody", "You have levelled up.");
        Entry.ExpirySeconds = 5.f;
        Entry.Metadata.Add("NewLevel", FString::FromInt(LastMessage.NewLevel));
        return Entry;
    }

    virtual void RegisterListener(
        UGameCoreEventSubsystem*               Bus,
        UGameCoreNotificationSubsystem*        Subsystem,
        TArray<FGameplayMessageListenerHandle>& OutHandles) override
    {
        if (!Bus) return;
        FGameplayMessageListenerHandle Handle =
            Bus->StartListening<FProgressionLevelUpMessage>(
                Channel,
                EGameCoreEventScope::ClientOnly,
                this,
                [this, Subsystem](FGameplayTag InChannel, const FProgressionLevelUpMessage& Msg)
                {
                    LastMessage = Msg; // Capture before BuildEntry reads it
                    FNotificationEntry Entry = BuildEntry(InChannel);
                    if (!Entry.CategoryTag.IsValid()) return;
                    Subsystem->HandleIncomingEntry(MoveTemp(Entry));
                });
        OutHandles.Add(Handle);
    }

    mutable FProgressionLevelUpMessage LastMessage;
};
```

---

## Notes

- `BuildEntry_Implementation` default returns a default-constructed `FNotificationEntry` (invalid `CategoryTag`) — always override.
- The `mutable LastMessage` pattern for C++ bindings is intentional: `BuildEntry` is `const` (Blueprint-NativeEvent constraint), but must read the most recent payload. The payload is written by the typed listener lambda immediately before `BuildEntry` is called on the same game thread, so there is no race condition.
- Blueprint bindings must guard against stale replicated state. Return an entry with invalid `CategoryTag` if data is not yet available.
- Event Bus events that trigger notifications should be `ClientOnly` scope. A `ServerOnly` event will only fire on listen-server or standalone builds. This is almost always a misconfiguration — document the expected scope in the binding's comment.
