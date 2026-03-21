# JournalTypes

**File:** `GameCore/Source/GameCore/Journal/JournalTypes.h`

All shared structs, message structs, and delegate declarations for the Journal System. No `.cpp` needed — header-only.

---

## `FJournalEntryHandle`

The atomic unit of the journal. Stored in the persisted array and sent to the client. Contains no content — only identity and acquisition time.

```cpp
// JournalTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalEntryHandle
{
    GENERATED_BODY()

    // Unique identity of this entry type. Maps to a UJournalEntryDataAsset
    // via UJournalRegistrySubsystem. Replicates as a network index — never a string.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag EntryTag;

    // Top-level track. Used for tab filtering on the client.
    // e.g. Journal.Track.Books | Journal.Track.Adventure
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag TrackTag;

    // Server UTC unix epoch at time of acquisition.
    UPROPERTY(BlueprintReadOnly)
    int64 AcquiredTimestamp = 0;
};
```

> **No `FGuid` per handle.** Identity is `EntryTag`. For repeating entries (daily quests), the same `EntryTag` appears multiple times with different `AcquiredTimestamp` values. Timestamp is sufficient for sort and display.

> **No asset reference in the handle.** `EntryTag` → asset is resolved via `UJournalRegistrySubsystem::GetEntryAsset()` on demand. Keeps the handle small and avoids string paths in replication.

---

## `FJournalRenderedDetails`

Output of `IJournalEntry::BuildDetails()`. Contains everything the UI needs to render one entry's content panel.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalRenderedDetails
{
    GENERATED_BODY()

    // UE Rich Text markup string. Supports inline images, font styles,
    // color runs, and custom decorators via URichTextBlock + UDataTable.
    UPROPERTY(BlueprintReadOnly)
    FText RichBodyText;

    // Optional header image displayed above body text.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UTexture2D> HeaderImage;

    // Extend here as needed: voice line cue, ambient audio, etc.
};
```

---

## `FJournalCollectionProgress`

Result of `UJournalRegistrySubsystem::GetCollectionProgress()`. Always derived at runtime — never stored per player.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalCollectionProgress
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 Found = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Total = 0;

    float Ratio() const
    {
        return Total > 0 ? static_cast<float>(Found) / Total : 0.f;
    }

    bool IsComplete() const { return Found >= Total && Total > 0; }
};
```

---

## `FJournalEntryAddedMessage`

Event Bus message broadcast by `UJournalComponent::AddEntry()` on the server.

```cpp
// Broadcast channel: GameCoreEvent.Journal.EntryAdded
// Scope: EGameCoreEventScope::ServerOnly
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

**Typical consumers (external systems — not Journal System itself):**
- Achievement system: checks if a collection is now complete
- Notification system: shows a toast on the client (via `OnEntryAdded` delegate instead)
- Audio system: plays a discovery sound

---

## Delegate Declarations

```cpp
// Fired on the client after Client_InitialJournalSync completes.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJournalSynced);

// Fired on the client when a new entry arrives during the session.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnJournalEntryAdded, FJournalEntryHandle, NewHandle);
```
