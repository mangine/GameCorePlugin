# JournalTypes

**File:** `GameCore/Source/GameCore/Journal/JournalTypes.h`  
**Purpose:** All plain data types, message structs, and delegate declarations used by the Journal System.

---

## `FJournalEntryHandle`

The atomic unit of the journal. Stored in `ServerPersistenceBuffer` (server) and `Entries` (client). Contains **no content** — only identity and acquisition time.

```cpp
// JournalTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalEntryHandle
{
    GENERATED_BODY()

    // Unique identity of this entry type. Maps to a UJournalEntryDataAsset
    // via UJournalRegistrySubsystem::GetEntryAsset(). Replicates as a
    // network index — never a string.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag EntryTag;

    // Top-level track this entry belongs to. Used for tab filtering on the client.
    // e.g. Journal.Track.Books | Journal.Track.Adventure
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag TrackTag;

    // Server UTC unix epoch (seconds) at time of acquisition.
    // Used for descending sort in GetPage().
    UPROPERTY(BlueprintReadOnly)
    int64 AcquiredTimestamp = 0;
};
```

> **No `FGuid` per handle.** Identity is `EntryTag`. For repeating entries the same `EntryTag` appears multiple times with different timestamps. Per-acquisition uniqueness is not required.

> **No asset reference in the handle.** `EntryTag` → asset is resolved on demand via `UJournalRegistrySubsystem::GetEntryAsset()`. Keeps the handle small and avoids string paths in replication.

---

## `FJournalRenderedDetails`

Output of `IJournalEntry::BuildDetails()`. Contains everything the UI needs to render one entry's content panel. Heavy assets (textures) are already loaded when this struct is constructed.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalRenderedDetails
{
    GENERATED_BODY()

    // UE Rich Text markup string. Compatible with URichTextBlock + UDataTable decorators.
    UPROPERTY(BlueprintReadOnly)
    FText RichBodyText;

    // Optional header image displayed above body text.
    // Loaded by the time BuildDetails calls OnReady.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UTexture2D> HeaderImage;

    // Extend here as needed: audio cue, ambient FX tag, etc.
};
```

---

## `FJournalCollectionProgress`

Result of `UJournalRegistrySubsystem::GetCollectionProgress()`. Always derived at runtime — never persisted per player.

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
        return Total > 0 ? static_cast<float>(Found) / static_cast<float>(Total) : 0.f;
    }

    bool IsComplete() const { return Total > 0 && Found >= Total; }
};
```

---

## `FJournalEntryAddedMessage`

Event Bus message broadcast by `UJournalComponent::AddEntry()` on the server.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalEntryAddedMessage
{
    GENERATED_BODY()

    // The PlayerState that received the entry.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<APlayerState> PlayerState = nullptr;

    // The handle that was added.
    UPROPERTY(BlueprintReadOnly)
    FJournalEntryHandle Handle;
};
```

**Channel tag:** `GameCoreEvent.Journal.EntryAdded`  
**Scope:** `EGameCoreEventScope::ServerOnly`  
**Broadcast site:** `UJournalComponent::AddEntry()` after `Client_AddEntry` is dispatched.

---

## Delegate Declarations

```cpp
// Fired on the client after Client_InitialJournalSync completes.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJournalSynced);

// Fired on the client when a new entry arrives via Client_AddEntry RPC.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnJournalEntryAdded, FJournalEntryHandle, NewHandle);
```
