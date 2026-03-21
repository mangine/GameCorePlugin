# Journal System — Architecture

**Module:** `GameCore` (plugin)  
**Status:** Specification — Pending Implementation  
**UE Version:** 5.7

---

## Purpose

The Journal System is a **persistent, server-authoritative lore and history tracker**. It records every significant event a player has experienced — books read, quests completed, places visited, events witnessed — as a chronological, queryable list of lightweight handles pointing to designer-authored data assets.

It is **not** a content system. It stores identity and acquisition time only. All display content (rich text, images, audio) lives in data assets and is loaded on demand by the client UI.

---

## Dependencies

### Unreal Engine Modules
| Module | Usage |
|---|---|
| `GameplayTags` | Entry and track identity, channel tags for event bus |
| `GameplayMessageSubsystem` (via EventBus) | Broadcasting `FJournalEntryAddedMessage` |
| `Engine` | `UActorComponent`, `UGameInstanceSubsystem`, `UPrimaryDataAsset` |
| `NetCore` | Client RPCs (`Client_InitialJournalSync`, `Client_AddEntry`) |

### GameCore Plugin Systems
| System | Usage |
|---|---|
| **Serialization System** | `UJournalComponent` implements `IPersistableComponent`; dirty-tracking via `NotifyDirty()`; `UPersistenceRegistrationComponent` already on `APlayerState` |
| **Event Bus System** | `UGameCoreEventBus::Broadcast<FJournalEntryAddedMessage>()` emitted server-side on each `AddEntry` call |

> The Journal System has **no dependency** on the Requirement System, State Machine, Interaction System, or any other GameCore module.

---

## Requirements

### Functional
- Record any type of entry (book, quest, event, place, dialogue, etc.) keyed by `FGameplayTag`
- Support repeating entries (same tag, multiple timestamps) — e.g. daily quests run multiple times
- Support non-repeating entries (books, places) with `O(1)` duplicate prevention
- Track entries across named **tracks** (`FGameplayTag TrackTag`) for independent tab filtering
- Support **collections**: named sets of entries with found/total progress, including nested sub-collections
- Paginate any filtered view (by track and/or collection) entirely client-side
- Provide on-demand content loading per entry — no heavy content held in RAM unless the UI requests it

### Performance & Scale
- Server holds only `TSet<FGameplayTag>` and `ServerPersistenceBuffer` per player — no redundant history in server RAM
- Full entry history sent to owning client **once** at login via RPC, then the temporary local array is dropped
- New entries replicated to client via targeted `Client_AddEntry` RPC — no full-array re-send
- `FJournalEntryHandle` is 24 bytes. 1000 entries ≈ 24 KB on the wire at login
- Server RAM cost at 1000 players × 1000 entries: `AcquiredSet` ≈ 8 MB + `ServerPersistenceBuffer` ≈ 24 MB

### Authority
- All mutations are **server-authoritative**. `AddEntry` is `BlueprintAuthorityOnly`
- Client data is read-only, populated by login sync RPC and incremental RPCs
- No client-side prediction of journal state

---

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Entry identity primitive | `FGameplayTag` | Replicates as a network index (not a string). Consistent with all other GameCore systems. |
| History storage on server | `TSet<FGameplayTag>` + `ServerPersistenceBuffer` | `AcquiredSet` for O(1) duplicate prevention; `ServerPersistenceBuffer` for serialization. No query history needed on server. |
| History storage on client | `TArray<FJournalEntryHandle>` + `TSet<FGameplayTag>` | Full ordered history for pagination; set for O(1) `HasEntry` queries. |
| Login sync mechanism | One-shot `Client_InitialJournalSync` RPC | Sent once at possession. Incremental `Client_AddEntry` handles subsequent additions. |
| Content loading | `IJournalEntry::BuildDetails()` async lambda on demand | Heavy assets (textures, referenced DTs) never live in the handle. Loaded only when UI requests a visible page. |
| Duplicate prevention | `TSet<FGameplayTag>` on server, checked at `AddEntry` | O(1). Rebuilt from `ServerPersistenceBuffer` on `Serialize_Load`. |
| Repeating entries | `bAllowDuplicates` flag on `AddEntry` | Same tag, multiple handles with different timestamps. Correct for daily/repeatable quests. |
| Tracks | `FGameplayTag TrackTag` on each handle | Routes entries to independent journal tabs. Filtering is client-side over the local array. |
| Collections | `UJournalCollectionDefinition` DataAsset | Designer-authored set of entry tags. Progress is always **derived** — acquired ∩ members. "Missing" entries are never stored. |
| Nested collections | `TArray<TSoftObjectPtr<UJournalCollectionDefinition>> SubCollections` | Arbitrary depth via asset picker. Circular refs caught in `IsDataValid()`. |
| Pagination | Client-only `GetPage()` on `UJournalComponent` | Server has no pagination concept. Client slices the local array after filtering. |
| Serialization | `TArray<FJournalEntryHandle>` via `IPersistableComponent` | Tag (FName network index) + int64 timestamp per entry. Compact, versioned, no server RAM waste. |
| Single component | `UJournalComponent` with track-tag filtering | No benefit to splitting by type. Simpler, cheaper, and easier to locate. |
| EventBus over GMS direct | `UGameCoreEventBus::Broadcast<>()` | Consistent with all other GameCore systems. Scope enforcement and payload validation for free. |
| Subsystem type | `UGameInstanceSubsystem` for `UJournalRegistrySubsystem` | Asset registry is per game-instance, not per-world. Survives seamless travel. |

---

## System Overview

```
[Server — APlayerState]
  UJournalComponent  (implements IPersistableComponent)
    TSet<FGameplayTag>           AcquiredSet           ← O(1) dup check, runtime only
    TArray<FJournalEntryHandle>  ServerPersistenceBuffer ← write buffer for Serialize_Save

[Client — owning client only]
  UJournalComponent
    TArray<FJournalEntryHandle>  Entries               ← full ordered history
    TSet<FGameplayTag>           ClientAcquiredSet     ← rebuilt from Entries on sync

[Both — UGameInstanceSubsystem]
  UJournalRegistrySubsystem
    TMap<FGameplayTag, TSoftObjectPtr<UJournalEntryDataAsset>>  EntryRegistry
    TMap<FGameplayTag, TObjectPtr<UJournalCollectionDefinition>> CollectionRegistry

[Content Browser — Game Module]
  UJournalEntryDataAsset subclasses  (implements IJournalEntry)
  UJournalCollectionDefinition assets
```

---

## Logic Flow

### Login / Possession
```
APlayerState possessed by player controller
  → Server: UJournalComponent::Serialize_Load(Ar, SavedVersion)
      Reads binary blob
      Rebuilds AcquiredSet
      Rebuilds ServerPersistenceBuffer
      Builds temporary TArray<FJournalEntryHandle> LoadedEntries
      Calls Client_InitialJournalSync(LoadedEntries)
      LoadedEntries goes out of scope — server frees it

  → Client: Client_InitialJournalSync_Implementation() received
      Entries = AllEntries
      ClientAcquiredSet rebuilt from Entries
      OnJournalSynced.Broadcast() → UI may refresh
```

### New Entry Acquired During Session
```
Game module calls UJournalComponent::AddEntry(EntryTag, TrackTag, bAllowDuplicates)
  → Authority check
  → Tag validity check
  → AcquiredSet.Contains() duplicate check (if !bAllowDuplicates)
  → Build FJournalEntryHandle (EntryTag, TrackTag, UtcTimestamp)
  → AcquiredSet.Add(EntryTag)   [idempotent]
  → ServerPersistenceBuffer.Add(Handle)
  → NotifyDirty(this)            ← propagates to UPersistenceRegistrationComponent
  → Client_AddEntry(Handle)      ← targeted RPC to owning client
  → UGameCoreEventBus::Broadcast<FJournalEntryAddedMessage>()

  → Client: Client_AddEntry_Implementation()
      Entries.Add(Handle)
      ClientAcquiredSet.Add(Handle.EntryTag)
      OnEntryAdded.Broadcast(Handle) → UI notification
```

### Save Cycle
```
UPersistenceRegistrationComponent::BuildPayload()
  → Iterates all IPersistableComponent on the actor
  → Calls UJournalComponent::Serialize_Save(Ar)
      Writes ServerPersistenceBuffer.Num() + each handle to Ar
  → Calls UJournalComponent::ClearIfSaved(FlushedGeneration)
      Clears bDirty if no newer dirty occurred during flush
```

### Collection Progress Query (Client)
```
UI calls UJournalRegistrySubsystem::GetCollectionProgress(CollectionTag, ClientAcquiredSet)
  → CollectionRegistry.Find(CollectionTag)
  → ComputeProgress() — recursive depth-first over Members + SubCollections
  → Returns FJournalCollectionProgress { Found, Total }
```

---

## Known Issues

| # | Issue | Impact | Recommended Fix |
|---|---|---|---|
| 1 | `GetPage()` allocates and sorts a full filtered copy every call. For large journals (1000+ entries) this is O(N log N) per page flip. | Client CPU / GC pressure | Cache filtered+sorted result per `(TrackFilter, CollectionFilter)` pair, invalidate on `OnEntryAdded`. |
| 2 | `BuildDetails()` on `IJournalEntry` takes a `TFunction` but is declared `virtual void` (non-UFUNCTION). Blueprint subclasses cannot override it. | Blueprint authoring limitation | Expose a `UFUNCTION(BlueprintNativeEvent)` wrapper or keep it C++ only and document explicitly. |
| 3 | `UJournalRegistrySubsystem::LoadAllEntryAssets()` does a **synchronous** load of all entry assets at game instance init. If the game module registers thousands of entries or assets with large embedded data, this will hitch. | Startup hitch on large projects | Prefer async load (`RequestAsyncLoad`) with a ready callback; flag subsystem as "not ready" until complete. |
| 4 | Circular sub-collection detection in `IsDataValid()` is documented but the detection algorithm is not specified. A naive visited-set check in `ComputeProgress` would infinite-loop without it. | Runtime crash on circular refs | Pass a `TSet<FGameplayTag> Visited` into `ComputeProgress` and `CollectMemberTags`; early-return + ensure if already visited. |
| 5 | `Client_InitialJournalSync` sends the full `TArray<FJournalEntryHandle>` as a single RPC. With 1000 entries at 24 bytes each this is 24 KB in one packet, which may exceed the default UE RPC payload limit (~65 KB minus overhead). | Potential RPC truncation on large journals | Batch the sync into chunks of e.g. 256 entries using a multi-RPC pattern, or use the `NetCore` reliable buffer increase. |
| 6 | The original spec uses `Serialize_Save` / `Serialize_Load` naming but the `IPersistableComponent` interface in GameCore Specifications 2 uses `Serialize_Save(FArchive&)` and `Serialize_Load(FArchive&, uint32)`. | API mismatch will cause compile errors | Align to the v2 interface: `Serialize_Load` takes `uint32 SavedVersion`. |

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Journal/
            ├── JournalTypes.h                       — FJournalEntryHandle, FJournalRenderedDetails,
            │                                           FJournalCollectionProgress, FJournalEntryAddedMessage,
            │                                           delegate declarations
            ├── JournalEntry.h / .cpp                — UJournalEntry (UInterface), IJournalEntry
            ├── JournalEntryDataAsset.h / .cpp       — UJournalEntryDataAsset (Abstract base)
            ├── JournalCollectionDefinition.h / .cpp — UJournalCollectionDefinition
            ├── JournalComponent.h / .cpp            — UJournalComponent
            └── JournalRegistrySubsystem.h / .cpp    — UJournalRegistrySubsystem
```

Concrete `UJournalEntryDataAsset` subclasses (e.g. `UBookJournalEntryDataAsset`) live in the **game module**.

### Build.cs Dependencies

```csharp
// GameCore.Build.cs — Journal adds no new module dependencies
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "GameplayMessageRuntime",  // already required by EventBus
    "NetCore",
});
```

### Gameplay Tags (GameCore DefaultGameplayTags.ini)

```ini
[/Script/GameplayTags.GameplayTagsSettings]
+GameplayTagList=(Tag="GameCoreEvent.Journal.EntryAdded", DevComment="Server-side: fired after a journal entry is successfully added")
+GameplayTagList=(Tag="Journal.Track",                   DevComment="Namespace — leaf track tags defined in game module")
```

Game module adds its own tags:
```ini
+GameplayTagList=(Tag="Journal.Track.Books",       DevComment="Lore books and scrolls")
+GameplayTagList=(Tag="Journal.Track.Adventure",   DevComment="Quests, events, places")
+GameplayTagList=(Tag="Journal.Entry",             DevComment="Namespace — all entry identity tags")
+GameplayTagList=(Tag="Journal.Collection",        DevComment="Namespace — all collection tags")
```
