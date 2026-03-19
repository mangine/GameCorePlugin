# GMS Integration

**Sub-page of:** [Notification System Overview](Notification%20System%20Overview.md)

This page covers how GMS channels are connected to the notification subsystem, how to write
channel bindings in C++ and Blueprint, and the listener registration pattern.

---

## How It Works

```
GMS channel fires (e.g. GameCoreEvent.Progression.LevelUp)
    └─► UNotificationChannelBinding (registered in UNotificationChannelConfig)
            └─► BuildEntry() returns FNotificationEntry
                    └─► UGameCoreNotificationSubsystem::HandleIncomingEntry
                            └─► PushEntryInternal → delegates fire → UI reacts
```

The subsystem never touches game-specific message structs. The binding is the only translation layer.

---

## C++ Binding (Typed Payload)

For channels with a known C++ struct, subclass `UNotificationChannelBinding` and hold a typed
listener inside the binding itself. The subsystem calls `RegisterChannelListeners` which invokes
`RegisterTypedListener()` on each binding.

```cpp
// In game module — NOT in GameCore.

UCLASS()
class ULevelUpNotificationBinding : public UNotificationChannelBinding
{
    GENERATED_BODY()
public:
    virtual FNotificationEntry BuildEntry_Implementation(const FGameplayTag& Channel) const override
    {
        // At the time BuildEntry is called, LastMessage has been populated
        // by the typed GMS listener below.
        FNotificationEntry Entry;
        Entry.CategoryTag    = FGameplayTag::RequestGameplayTag("Notification.Category.Progression");
        Entry.Title          = FText::Format(
            LOCTEXT("LevelUp", "Level {0}!"), FText::AsNumber(LastMessage.NewLevel));
        Entry.Body           = LOCTEXT("LevelUpBody", "You have levelled up.");
        Entry.ExpirySeconds  = 5.f;
        Entry.Metadata.Add("NewLevel", FString::FromInt(LastMessage.NewLevel));
        return Entry;
    }

    // Called by the subsystem's typed GMS listener before BuildEntry.
    // The subsystem registers this listener when iterating ChannelConfig->Bindings.
    // See RegisterTypedListener pattern below.
    mutable FProgressionLevelUpMessage LastMessage;
};
```

### Typed Listener Registration Pattern

The subsystem's `RegisterChannelListeners` must be extended to support typed bindings.
The clean approach is a virtual on `UNotificationChannelBinding`:

```cpp
// In UNotificationChannelBinding:
class UNotificationChannelBinding
{
    // ...

    // Override to register a typed GMS listener and populate internal state before BuildEntry.
    // Default implementation registers a no-op listener and calls BuildEntry directly.
    // The subsystem passes itself so the binding can call HandleIncomingEntry.
    virtual void RegisterListener(
        UGameplayMessageSubsystem* GMS,
        UGameCoreNotificationSubsystem* Subsystem,
        TArray<FGameplayMessageListenerHandle>& OutHandles);
};
```

Default implementation (for Blueprint bindings):

```cpp
void UNotificationChannelBinding::RegisterListener(
    UGameplayMessageSubsystem* GMS,
    UGameCoreNotificationSubsystem* Subsystem,
    TArray<FGameplayMessageListenerHandle>& OutHandles)
{
    // Registers a listener that fires on ANY message on Channel.
    // Uses FGameplayTag as a dummy type — actual payload is ignored at this level.
    // Blueprint bindings query game state themselves inside BuildEntry.
    FGameplayMessageListenerHandle Handle =
        GMS->RegisterListener<FGameplayTag>(
            Channel,
            [this, Subsystem](FGameplayTag InChannel, const FGameplayTag&)
            {
                FNotificationEntry Entry = BuildEntry(InChannel);
                if (!Entry.CategoryTag.IsValid()) return; // Binding suppressed
                Subsystem->HandleIncomingEntry(Entry);
            });
    OutHandles.Add(Handle);
}
```

C++ typed override for `ULevelUpNotificationBinding`:

```cpp
void ULevelUpNotificationBinding::RegisterListener(
    UGameplayMessageSubsystem* GMS,
    UGameCoreNotificationSubsystem* Subsystem,
    TArray<FGameplayMessageListenerHandle>& OutHandles)
{
    FGameplayMessageListenerHandle Handle =
        GMS->RegisterListener<FProgressionLevelUpMessage>(
            Channel,
            [this, Subsystem](FGameplayTag InChannel, const FProgressionLevelUpMessage& Msg)
            {
                LastMessage = Msg;  // Capture before BuildEntry reads it.
                FNotificationEntry Entry = BuildEntry(InChannel);
                if (!Entry.CategoryTag.IsValid()) return;
                Subsystem->HandleIncomingEntry(Entry);
            });
    OutHandles.Add(Handle);
}
```

Subsystem `RegisterChannelListeners` then becomes simply:

```cpp
void UGameCoreNotificationSubsystem::RegisterChannelListeners()
{
    if (!ChannelConfig) return;

    UGameplayMessageSubsystem* GMS =
        GetWorld() ? GetWorld()->GetSubsystem<UGameplayMessageSubsystem>() : nullptr;
    if (!GMS) return;

    for (UNotificationChannelBinding* Binding : ChannelConfig->Bindings)
    {
        if (Binding && Binding->Channel.IsValid())
            Binding->RegisterListener(GMS, this, ListenerHandles);
    }
}
```

---

## Blueprint Binding

Blueprint bindings cannot receive a typed struct from GMS. Instead, they use the GMS event as a
trigger and pull the data they need from already-replicated game state.

**Example:** A Blueprint `UQuestNotificationBinding` that fires when a quest starts:

1. Create a Blueprint subclass of `UNotificationChannelBinding`.
2. Set `Channel` to `GameCoreEvent.Quest.Started`.
3. Override `BuildEntry` (Blueprint event):
   - Get the local `APlayerController` via `GetWorld()->GetFirstPlayerController()`.
   - Read quest data from its `UQuestComponent` (already replicated to client).
   - Fill and return an `FNotificationEntry`.
4. Return a default entry with an **invalid `CategoryTag`** to suppress the notification
   (e.g. if the quest data isn't ready yet).

```
[BuildEntry Override in Blueprint]
  → Get Player Controller → Get Quest Component
  → Find quest by tag from replicated data
  → If found: fill Entry (CategoryTag, Title, Body, ExpirySeconds=8)
  → If not found: return empty Entry (CategoryTag = none → suppressed)
```

> **Suppression contract**: `HandleIncomingEntry` checks `Entry.CategoryTag.IsValid()` before pushing. An invalid category tag = silent suppress. Bindings use this to skip notifications for events they don't have data for yet.

---

## `HandleIncomingEntry` (Internal)

This method is `public` only to allow bindings to call it. It is not intended as a game-layer API — use `PushNotification` for direct pushes.

```cpp
void UGameCoreNotificationSubsystem::HandleIncomingEntry(FNotificationEntry Entry)
{
    // Guard: must have a valid category.
    if (!Entry.CategoryTag.IsValid())
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("UGameCoreNotificationSubsystem: binding returned entry with invalid CategoryTag — suppressed."));
        return;
    }
    PushEntryInternal(Entry);
}
```

---

## GMS Scope Note

GMS events that trigger notifications are typically `ClientOnly` — the data has been replicated to
the client already, and the client fires a local GMS broadcast to trigger UI. The Notification
System sits entirely in client space and does not care about scope; it only sees events that have
already been dispatched on the local machine.

If a `ServerOnly` GMS event is subscribed to in a binding, it will only fire in listen-server or
standalone builds, not on a dedicated client. This is almost certainly a misconfiguration —
document this restriction clearly in the binding's comment.
