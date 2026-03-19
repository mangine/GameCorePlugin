# GMS Events

**Sub-page of:** [Journal System](../Journal%20System.md)

All events are broadcast via `UGameCoreEventSubsystem` (GMS). The Journal System emits events — it does not subscribe to any.

External systems (achievement systems, UI notification managers, audio systems) subscribe to these events and react accordingly. The journal system never knows about its consumers.

---

## Events Emitted

### `GameCoreEvent.Journal.EntryAdded`

Fired on the **server** when a new entry is successfully added to a player's journal.

```cpp
// JournalTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalEntryAddedMessage
{
    GENERATED_BODY()

    // The PlayerState that received the entry.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<APlayerState> PlayerState;

    // The handle that was added.
    UPROPERTY(BlueprintReadOnly)
    FJournalEntryHandle Handle;
};
```

**Broadcast site:** `UJournalComponent::AddEntry()`, after `Client_AddEntry()` is dispatched.

**Typical consumers:**
- Achievement system: checks if a collection is now complete
- UI notification system: shows a "New Journal Entry" toast on the client (subscribe to `OnEntryAdded` delegate instead for client-side)
- Audio system: plays a discovery sound

**Channel tag:** `GameCoreEvent.Journal.EntryAdded`

---

## Events Consumed

The Journal System has **zero GMS subscriptions**. It exposes `AddEntry()` as a public server API. The game module's wiring layer subscribes to external events and calls `AddEntry()` directly.

```
// Pattern — lives in game module bridge component on APlayerState:

GMS.Subscribe(GameCoreEvent.Quest.Completed, [this](FQuestCompletedMessage Msg)
{
    UJournalComponent* Journal = PlayerState->FindComponentByClass<UJournalComponent>();
    Journal->AddEntry(
        Msg.QuestCompletedTag,   // EntryTag — maps to a UQuestJournalEntryDataAsset
        TAG_Journal_Track_Adventure,
        true);  // bAllowDuplicates = true for repeatable quests
});
```

---

## Tag Declarations

```
GameCoreEvent
  Journal
    EntryAdded

Journal
  Track
    Books
    Adventure
    (game-specific tracks defined in game module tags)
  Entry
    (all entry tags defined in game module — GameCore does not own entry tags)
  Collection
    (all collection tags defined in game module)
```

> **GameCore owns:** `GameCoreEvent.Journal.*` channel tags, `Journal.Track.*` base namespace.  
> **Game module owns:** `Journal.Entry.*` and `Journal.Collection.*` — these are content-specific and must not live in the plugin.
