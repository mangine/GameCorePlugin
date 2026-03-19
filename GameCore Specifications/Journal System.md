# Journal System

**Module:** `GameCore` (plugin)  
**Status:** Specification — Pending Implementation  
**UE Version:** 5.7  
**Depends On:** GameCore (Serialization System, Event Bus)

---

## Sub-Pages

| File | Contents |
|---|---|
| [Data Assets and Definitions](Journal%20System/Data%20Assets%20and%20Definitions.md) | `IJournalEntry`, `UJournalEntryDataAsset`, `UJournalCollectionDefinition`, `FJournalEntryHandle` |
| [UJournalComponent](Journal%20System/UJournalComponent.md) | Per-player component: add entry, persistence, login sync, client query API |
| [UJournalRegistrySubsystem](Journal%20System/UJournalRegistrySubsystem.md) | World subsystem: asset registry, collection loading, progress queries |
| [GMS Events](Journal%20System/GMS%20Events.md) | All GameplayMessage events emitted by the journal system |
| [File Structure](Journal%20System/File%20Structure.md) | Module layout, Build.cs dependencies, gameplay tags |
| [Integration Guide](Journal%20System/Integration%20Guide.md) | How to wire the journal from the game module, usage samples |

---

## Purpose

The Journal System is a **persistent, server-authoritative lore and history tracker**. It records every significant event a player has experienced — books read, quests completed, places visited, events witnessed — as a chronological, queryable list of lightweight handles that point to designer-authored data assets.

It is **not** a content system. It stores identity and acquisition time only. All display content (rich text, images, audio) lives in data assets and is loaded on demand by the client UI.

---

## Requirements

### Functional
- Record any type of entry (book, quest, event, place, dialogue, etc.) keyed by `FGameplayTag`
- Support repeating entries (same tag, multiple timestamps) — e.g. daily quests run multiple times
- Support non-repeating entries (books, places) with duplicate prevention
- Track entries across named **tracks** (e.g. Books, Adventure) for independent tab filtering
- Support **collections**: named sets of entries with found/total progress, including nested sub-collections
- Paginate any filtered view (by track and/or collection) entirely client-side
- Provide on-demand content loading per entry — no content held in RAM unless the UI requests it

### Performance & Scale
- Server holds only `TSet<FGameplayTag>` per player — no history array in server RAM after login
- Full entry history sent to owning client once at login, discarded from server RAM immediately after
- New entries replicated to client via targeted `ClientRPC` — no full-array re-send
- Each `FJournalEntryHandle` is 16–24 bytes. 1000 entries ≈ 24 KB on the wire at login
- Server RAM cost: `TSet<FGameplayTag>` × player count — approximately 8 bytes × entry count per player

### Authority
- All mutations are **server-authoritative**. `AddEntry` is server-only
- Client data is read-only, populated by login sync and incremental RPCs
- No client-side prediction of journal state

---

## Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Entry identity primitive | `FGameplayTag` | Replicates as a network index (not a string). Consistent with all other GameCore systems. Asset path (`TSoftObjectPtr`) is unacceptable for replication. |
| History storage on server | `TSet<FGameplayTag>` only (no `TArray`) | Server has no use for history order or timestamps. Holding full history for 1000+ players × 1000+ entries wastes RAM. |
| History storage on client | `TArray<FJournalEntryHandle>` | Client owns full history for local pagination and display. Never re-fetched after login unless explicitly invalidated. |
| Login sync mechanism | One-shot server RPC dispatching full `TArray` | Sent once at possession, then discarded from server. Incremental `ClientRPC` handles subsequent additions. |
| Content loading | `IJournalEntry::BuildDetails()` async on demand | Rich text, images, and other heavy content never live in the handle. Loaded only when the UI requests a visible page. |
| Duplicate prevention | `TSet<FGameplayTag>` on server, checked at `AddEntry` | O(1). The set is rebuilt from the persisted array on `DeserializeFromSave`. Never persisted directly. |
| Repeating entries | `bAllowDuplicates` flag on `AddEntry` | Same tag, multiple handles with different timestamps. Correct for daily/repeatable quests. |
| Tracks | `FGameplayTag TrackTag` on each handle | Routes entries to independent journal tabs. Filtering is client-side over the local array. |
| Collections | `UJournalCollectionDefinition` DataAsset | Designer-authored set of entry tags. Progress is always **derived** (found = acquired ∩ members). "Missing" entries are never stored. |
| Nested collections | `TArray<TSoftObjectPtr<UJournalCollectionDefinition>> SubCollections` | Fully supported in UE editor via asset picker. Progress is recursive union of members + sub-collections. |
| Pagination | Client-only query API on `UJournalComponent` | Server has no pagination concept. Client slices the local array after filtering. |
| Serialization | `TArray<FJournalEntryHandle>` binary via `IPersistableComponent` | Tag (FName index) + int64 timestamp per entry. Compact, fast, schema-versioned. |
| Single component | One `UJournalComponent` with track-tag filtering | No benefit to splitting by type. A single component with tag filtering is simpler, cheaper, and easier to find. |

---

## System Overview

```
[Server]
  UJournalComponent
    TSet<FGameplayTag>  AcquiredSet     ← runtime only, rebuilt on load
    (TArray discarded after login sync)

  UJournalRegistrySubsystem
    TMap<FGameplayTag, TSoftObjectPtr<UJournalEntryDataAsset>>  EntryRegistry
    TMap<FGameplayTag, UJournalCollectionDefinition*>            Collections

[Client]
  UJournalComponent (owning client only)
    TArray<FJournalEntryHandle>  Entries       ← full history, local
    TSet<FGameplayTag>           AcquiredSet   ← rebuilt on login sync

[Content Browser]
  UJournalEntryDataAsset  (implements IJournalEntry)
  UJournalCollectionDefinition
```

---

## Login Flow

```
Player logs in → APlayerState possessed
  → Server: UJournalComponent::DeserializeFromSave()
      Reads binary blob
      Populates AcquiredSet (TSet<FGameplayTag>)
      Builds TArray<FJournalEntryHandle> LoadedEntries (temporary)
      Calls Client_InitialJournalSync(LoadedEntries)
      Empties LoadedEntries — server discards history

  → Client: Client_InitialJournalSync() received
      Populates Entries (TArray<FJournalEntryHandle>)
      Rebuilds AcquiredSet from Entries
      Fires OnJournalSynced delegate → UI may refresh

New entry acquired during session:
  → Server: AddEntry(EntryTag, TrackTag, bAllowDuplicates)
      AcquiredSet.Contains() check
      Appends to persisted binary blob (MarkDirty)
      Calls Client_AddEntry(FJournalEntryHandle)

  → Client: Client_AddEntry() received
      Entries.Add(Handle)
      AcquiredSet.Add(Handle.EntryTag)
      Fires OnEntryAdded delegate → UI notification
```

---

## How to Use This System

See [Integration Guide](Journal%20System/Integration%20Guide.md) for full wiring instructions and samples.

Quick summary:
1. Add `UJournalComponent` to `APlayerState`
2. Add `UPersistenceRegistrationComponent` and tag it `Persistence.Entity.Player` (shared with other components)
3. Create `UJournalEntryDataAsset` subclasses in the game module, implement `IJournalEntry`
4. Create `UJournalCollectionDefinition` assets in the Content Browser
5. From the game module, subscribe to GMS events and call `UJournalComponent::AddEntry()` on the server
6. From the client UI, call `GetPage()` to paginate, `BuildDetails()` per handle to load content
