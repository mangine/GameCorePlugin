# Journal System — Architecture

**Module:** `GameCore` (plugin)  
**UE Version:** 5.7  
**Status:** Specification — Pending Implementation  

---

## Purpose

The Journal System is a **persistent, server-authoritative lore and history tracker**. It records every significant event a player has experienced — books read, quests completed, places visited, events witnessed — as a chronological, queryable list of lightweight handles that point to designer-authored data assets.

It is **not** a content system. It stores identity and acquisition time only. All display content (rich text, images, audio) lives in data assets and is loaded on demand by the client UI.

---

## Dependencies

### GameCore Plugin Systems
| System | Usage |
|---|---|
| **Serialization System** (`IPersistableComponent`, `UPersistenceRegistrationComponent`) | `UJournalComponent` implements `IPersistableComponent` to persist the entry history blob. |
| **Event Bus System** (`UGameCoreEventBus`) | `UJournalComponent` broadcasts `GameCoreEvent.Journal.EntryAdded` on the server when an entry is added. |

### Unreal Engine Modules
| Module | Reason |
|---|---|
| `Core`, `CoreUObject`, `Engine` | Standard dependencies |
| `GameplayTags` | `FGameplayTag` is the primary entry identity primitive |
| `GameplayMessageSubsystem` | Underlying bus for event broadcasts |
| `NetCore` | Server→client RPC transport for login sync and incremental entry delivery |

### Runtime Dependencies
- `UPersistenceRegistrationComponent` must be present on the same actor as `UJournalComponent` (typically `APlayerState`). The component self-registers via `GetPersistenceKey()`.
- `UJournalRegistrySubsystem` (`UGameInstanceSubsystem`) must be running. It is auto-created by UE at game instance startup.
- `UAssetManager` must be configured with `JournalEntry` and `JournalCollection` primary asset type scans (see `DefaultGame.ini` block in `UJournalRegistrySubsystem.md`).

---

## Requirements

### Functional
- Record any type of entry (book, quest, event, place, dialogue, etc.) keyed by `FGameplayTag`
- Support **repeating entries** (same tag, multiple timestamps) — e.g. daily quests run multiple times
- Support **non-repeating entries** (books, places) with duplicate prevention
- Track entries across named **tracks** (e.g. Books, Adventure) for independent tab filtering
- Support **collections**: named sets of entries with found/total progress, including nested sub-collections
- Paginate any filtered view (by track and/or collection) entirely client-side
- Provide on-demand content loading per entry — no content held in RAM unless the UI requests it

### Performance & Scale
- Server holds only `TSet<FGameplayTag>` + `ServerPersistenceBuffer` per player after login sync — no redundant history array in server RAM
- Full entry history sent to owning client once at login, temporary server copy discarded immediately after
- New entries replicated to client via targeted `Client` RPC — no full-array re-send
- Each `FJournalEntryHandle` is 24 bytes. 1000 entries ≈ 24 KB on the wire at login
- Server RAM: `AcquiredSet` (~8 bytes × N) + `ServerPersistenceBuffer` (~24 bytes × N) per player

### Authority
- All mutations are **server-authoritative**. `AddEntry` is `BlueprintAuthorityOnly`
- Client data is read-only, populated by login sync RPC and incremental RPCs
- No client-side prediction of journal state

---

## Features

- Server-authoritative `AddEntry` with O(1) duplicate prevention via `TSet<FGameplayTag>`
- Login sync: full history RPC to owning client on possession
- Incremental RPC: single-handle delivery per new entry during session
- Client pagination API: `GetPage()` with track + collection filters, sorted by timestamp descending
- Collection progress: derived at runtime from `ClientAcquiredSet` — never stored per-player
- Nested sub-collections: arbitrary depth, circular-ref protection in editor validation
- Async content loading: `BuildDetails()` per entry, called only for visible UI pages
- Event Bus broadcast on new entry: allows achievement, audio, and notification systems to react without coupling
- Schema versioning + migration via `IPersistableComponent`
- Editor validation on data assets (`IsDataValid`): tag presence, track consistency, circular collection refs

---

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Entry identity primitive | `FGameplayTag` | Replicates as a network index (not a string). Consistent with all other GameCore systems. |
| History storage on server | `TSet<FGameplayTag>` (dedup) + `ServerPersistenceBuffer` (write-only) | Server needs dedup O(1) and a full payload for serialization. No query history needed. |
| History storage on client | `TArray<FJournalEntryHandle>` | Client owns full history for local pagination. Never re-fetched after login unless invalidated. |
| Login sync mechanism | One-shot `Client` RPC dispatching full `TArray` | Sent once at possession, server temp copy discarded after. |
| Content loading | `IJournalEntry::BuildDetails()` async on demand | Heavy assets (textures, rich text) never live in the handle. Loaded only when UI requests a visible page. |
| Duplicate prevention | `TSet<FGameplayTag>` on server, checked in `AddEntry` | O(1). Rebuilt from persistence buffer on `Serialize_Load`. |
| Repeating entries | `bAllowDuplicates` flag on `AddEntry` | Same tag, multiple handles with different timestamps. Correct for daily/repeatable quests. |
| Tracks | `FGameplayTag TrackTag` on each handle | Routes entries to independent journal tabs. Filtering is client-side over the local array. |
| Collections | `UJournalCollectionDefinition` DataAsset | Designer-authored set of entry tags. Progress is always **derived** (found = acquired ∩ members). "Missing" entries are never stored per-player. |
| Nested collections | `TArray<TSoftObjectPtr<UJournalCollectionDefinition>>` | Fully supported in UE editor via asset picker. Progress is recursive union of members + sub-collections. |
| Pagination | Client-only query API on `UJournalComponent` | Server has no pagination concept. Client slices the local array after filtering. |
| Serialization | `TArray<FJournalEntryHandle>` binary via `IPersistableComponent` | Tag index + timestamp per entry. Compact, fast, schema-versioned. |
| Registry | `UGameInstanceSubsystem` | Outlives world loads; consistent on both server and client. Entry asset list is static content — no per-player state. |
| Event broadcast | `UGameCoreEventBus` (`EGameCoreEventScope::ServerOnly`) | Decouples journal from consumers. Journal never knows about achievements, audio, or notifications. |

---

## Logic Flow

### System Architecture

```
[Server]
  UJournalComponent (on APlayerState)
    TSet<FGameplayTag>           AcquiredSet            ← O(1) dedup, rebuilt on load
    TArray<FJournalEntryHandle>  ServerPersistenceBuffer ← write-only, serialization source

  UJournalRegistrySubsystem (UGameInstanceSubsystem)
    TMap<FGameplayTag, TSoftObjectPtr<UJournalEntryDataAsset>>  EntryRegistry
    TMap<FGameplayTag, TObjectPtr<UJournalCollectionDefinition>> CollectionRegistry

[Client — owning client only]
  UJournalComponent
    TArray<FJournalEntryHandle>  Entries           ← full history, local
    TSet<FGameplayTag>           ClientAcquiredSet  ← rebuilt on login sync

  UJournalRegistrySubsystem
    (same asset registry — entry assets loaded on both sides)

[Content Browser — game module]
  UJournalEntryDataAsset  (implements IJournalEntry)
  UJournalCollectionDefinition
```

### Login Flow

```
Player logs in → APlayerState possessed
  → Server: IPersistableComponent::Serialize_Load() called by UPersistenceSubsystem
      Reads binary blob
      Populates AcquiredSet (TSet<FGameplayTag>)
      Populates ServerPersistenceBuffer (write-only, persists only)
      Builds LoadedEntries TArray (temporary)
      Calls Client_InitialJournalSync(LoadedEntries)
      Drops LoadedEntries — server discards query history

  → Client: Client_InitialJournalSync() received
      Populates Entries (TArray<FJournalEntryHandle>)
      Rebuilds ClientAcquiredSet from Entries
      Fires OnJournalSynced delegate → UI may refresh
```

### AddEntry Flow (mid-session)

```
Server: AddEntry(EntryTag, TrackTag, bAllowDuplicates)
  → Authority guard (return if not server)
  → Tag validity check
  → AcquiredSet.Contains() duplicate check (skip if !bAllowDuplicates and already acquired)
  → Build FJournalEntryHandle (tag + track + UTC timestamp)
  → AcquiredSet.Add(EntryTag)
  → ServerPersistenceBuffer.Add(Handle)
  → NotifyDirty() → UPersistenceRegistrationComponent marks entity dirty
  → Client_AddEntry(Handle) → owning client RPC
  → UGameCoreEventBus::Broadcast<FJournalEntryAddedMessage>(..., ServerOnly)

Client: Client_AddEntry() received
  → Entries.Add(Handle)
  → ClientAcquiredSet.Add(Handle.EntryTag)
  → OnEntryAdded.Broadcast(Handle) → UI notification
```

### GetPage Flow (client, UI driven)

```
UI: JournalComponent->GetPage(TrackFilter, CollectionFilter, PageIndex, PageSize)
  → If CollectionFilter valid:
      UJournalRegistrySubsystem::GetCollectionMemberTags(CollectionFilter)
          → recursive CollectMemberTags → TSet<FGameplayTag>
  → Linear scan Entries:
      Filter by TrackTag (if TrackFilter set)
      Filter by CollectionTags set (if CollectionFilter set)
  → Sort Filtered descending by AcquiredTimestamp
  → Slice [PageIndex*PageSize, +PageSize)
  → Return TArray<FJournalEntryHandle>
```

### Content Render Flow (client, per entry)

```
UI: ShowEntry(Handle)
  → UJournalRegistrySubsystem::GetEntryAsset(Handle.EntryTag)
      Returns TSoftObjectPtr<UJournalEntryDataAsset>
  → RequestAsyncLoad(AssetRef)
      → Asset already in memory (loaded at registry init) — callback fires immediately
  → Asset->BuildDetails(OnReady)
      → Book: synchronous, fills FJournalRenderedDetails from authored fields
      → Quest: RequestAsyncLoad(QuestDefinition) → fills FJournalRenderedDetails async
  → OnReady: UI renders RichBodyText + HeaderImage
```

---

## Known Issues

| # | Issue | Severity | Resolution |
|---|---|---|---|
| 1 | **`ServerPersistenceBuffer` memory growth** — grows unbounded during long sessions. 1000 entries/player × 1000 players = 24 MB. Acceptable for a dedicated server, but no cap exists. | Low | By design. Document the bound. Consider a cap + truncation policy if per-player entry budgets are introduced. |
| 2 | **Circular sub-collection detection** is editor-only (`IsDataValid`). A circular ref at runtime in `CollectMemberTags` / `ComputeProgress` would recurse infinitely. | Medium | Add a `TSet<FGameplayTag> Visited` guard in `CollectMemberTags` and `ComputeProgress` at runtime (cheap, TSet is stack-local). |
| 3 | **`GetPage` allocates and sorts on every call.** For large entry counts (1000+) and frequent UI refreshes this is noticeable. | Low | Cache last filter result invalidated by `OnEntryAdded`. Only re-sort when cache is stale. |
| 4 | **`BuildDetails` callback signature is a raw `TFunction`**, not a delegate or `TWeakObjectPtr`-guarded callback. If the UI widget is destroyed before the async load completes, the callback fires on a dangling widget. | Medium | Caller must guard: capture a `TWeakObjectPtr<UWidget>` and check validity in the lambda before accessing `this`. Document this clearly. |
| 5 | **No explicit `GetClientAcquiredSet()` accessor** is defined in the component spec. The integration guide references it. | Low | Add `const TSet<FGameplayTag>& GetClientAcquiredSet() const` as a public accessor. |
| 6 | **`UJournalRegistrySubsystem` is a `UGameInstanceSubsystem`** but the original spec's `GetPage` resolves it via `GetWorld()->GetGameInstance()`. This is fragile — subsystem access should always use `GetGameInstance()->GetSubsystem<T>()` directly from a world context. | Low | `UJournalComponent` can cache the subsystem pointer at `BeginPlay`. |

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Journal/
            ├── JournalTypes.h                        -- FJournalEntryHandle, FJournalRenderedDetails,
            │                                            FJournalCollectionProgress,
            │                                            FJournalEntryAddedMessage,
            │                                            FOnJournalSynced, FOnJournalEntryAdded
            ├── JournalEntry.h / .cpp                 -- UJournalEntry (UInterface), IJournalEntry
            ├── JournalEntryDataAsset.h / .cpp         -- UJournalEntryDataAsset (Abstract base)
            ├── JournalCollectionDefinition.h / .cpp  -- UJournalCollectionDefinition
            ├── JournalComponent.h / .cpp             -- UJournalComponent
            └── JournalRegistrySubsystem.h / .cpp     -- UJournalRegistrySubsystem
```

Concrete `UJournalEntryDataAsset` subclasses live in the **game module**, not in GameCore.

### Build.cs

```csharp
// GameCore.Build.cs — journal adds no new external dependencies
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "GameplayMessageRuntime",  // UGameplayMessageSubsystem (already present for Event Bus)
    "NetCore",                 // Fast TArray replication
});
```

```csharp
// Game module Build.cs — for concrete entry asset subclasses
PublicDependencyModuleNames.AddRange(new string[] { "GameCore", "GameplayTags" });
```

### Gameplay Tags — GameCore

```ini
; DefaultGameplayTags.ini (GameCore plugin)
+GameplayTagList=(Tag="GameCoreEvent.Journal.EntryAdded",DevComment="Server-side: fired when a journal entry is added to a player")
+GameplayTagList=(Tag="Journal.Track",DevComment="Namespace for track tags — leaf tags defined in game module")
```

### Gameplay Tags — Game Module

```ini
; DefaultGameplayTags.ini (game module)
+GameplayTagList=(Tag="Journal.Track.Books",DevComment="Lore books and scrolls track")
+GameplayTagList=(Tag="Journal.Track.Adventure",DevComment="Quests, events, places track")
+GameplayTagList=(Tag="Journal.Entry",DevComment="Namespace for all entry identity tags — defined per content")
+GameplayTagList=(Tag="Journal.Collection",DevComment="Namespace for all collection tags — defined per content")
```
