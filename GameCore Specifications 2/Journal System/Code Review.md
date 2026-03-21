# Journal System — Code Review

---

## Overview

The Journal System is well-conceived at the architecture level. The server/client split — server authoritative with minimal RAM footprint, client holding the full query history — is correct and pragmatic for an MMORPG. The `FGameplayTag` as the identity primitive is consistent with the rest of GameCore. The data asset approach for content separation is sound. The collection progress derivation model (never store per-player, always compute) is good.

However, several issues in the original spec range from minor inconsistencies to real bugs that must be addressed before implementation.

---

## Issues Found

### 1. Incomplete `IPersistableComponent` Alignment — Method Names Wrong

**Severity: High (build-breaking)**

The original spec uses `SerializeForSave` / `DeserializeFromSave` (old naming). The `IPersistableComponent` in GameCore Specifications 2 uses `Serialize_Save` / `Serialize_Load(Ar, SavedVersion)`. The `UJournalComponent` must use the correct interface names or it won't compile against the actual interface.

**Fix applied:** All implementations in this spec use `Serialize_Save`, `Serialize_Load(FArchive&, uint32)`, and `ClearIfSaved(uint32)`. The `NotifyDirty` pattern is also corrected to match the `IPersistableComponent` documentation (component owns dirty state, not the interface).

---

### 2. Circular Sub-Collection Reference — Runtime Crash Risk

**Severity: High**

The original spec adds `IsDataValid` circular ref detection as editor-only. However, `CollectMemberTags` and `ComputeProgress` have no runtime guard. Malformed data (or data that bypassed the editor, e.g. loaded from a binary cooked build with a bug) would recurse infinitely and crash the server.

**Fix applied:** Both `CollectMemberTags` and `ComputeProgress` now take a `TSet<FGameplayTag>& Visited` parameter and check/add `Collection->CollectionTag` before descending. Stack-local, negligible cost.

---

### 3. `GetPage` Allocates and Sorts on Every Call

**Severity: Medium (performance)**

The client paginates by building a full `Filtered` array, sorting it, then slicing. For 1000+ entries and a journal UI that refreshes on every scroll event or notification, this is a noticeable frame spike. It also produces a new `TArray` allocation every call.

**Recommendation (not applied to spec — improvement for implementation phase):** Cache the last filter result (track + collection key, sorted array). Invalidate on `OnEntryAdded`. `GetPage` slices the cached array. `GetFilteredCount` returns `CachedFiltered.Num()` directly. Adds ~3 lines of cache management, eliminates per-frame sort.

---

### 4. `BuildDetails` Callback with No Lifetime Guard — Dangling Widget Risk

**Severity: Medium**

The original `BuildDetails` signature takes a raw `TFunction<void(FJournalRenderedDetails)>`. If the widget issuing the call is destroyed before the async load completes (e.g. player closes the journal while a quest definition is being streamed), the callback fires on a dead `this`.

**Fix applied:** The Usage guide documents the `TWeakObjectPtr` guard pattern that callers must use. The interface itself cannot enforce this without changing its signature (which would add complexity for synchronous entry types).

**Recommendation:** Consider `TWeakObjectPtr<UObject> Requester` as an optional second parameter, allowing the interface to skip the callback if the requester is invalid — but this adds complexity for a pattern that is standard in UE UI development. Documentation is sufficient.

---

### 5. Missing `GetClientAcquiredSet()` Accessor

**Severity: Medium (missing feature)**

The original spec's `Integration Guide` references `Journal->GetClientAcquiredSet()` but no such method was declared in `UJournalComponent`. The collection progress widget depends on it.

**Fix applied:** `GetClientAcquiredSet()` added as `const TSet<FGameplayTag>& GetClientAcquiredSet() const { return ClientAcquiredSet; }` on `UJournalComponent`.

---

### 6. `UJournalRegistrySubsystem` Type Mismatch

**Severity: Low**

The original spec declares `CollectionRegistry` as `TMap<FGameplayTag, UJournalCollectionDefinition*>` in the class definition but uses `TObjectPtr<UJournalCollectionDefinition>` in method bodies. Inconsistent, and raw `*` pointers for UObjects should not be used in UObject-managed maps.

**Fix applied:** `CollectionRegistry` is uniformly declared as `TMap<FGameplayTag, TObjectPtr<UJournalCollectionDefinition>>`.

---

### 7. `FStreamableHandle` Not Kept Alive

**Severity: Medium**

The original spec stores `FStreamableHandle EntryLoadHandle` as a value member. `FStreamableHandle` returned from `RequestSyncLoad` keeps loaded assets from being GC'd while it's alive. Storing it as a value and then letting it go out of scope (or defaulting to an uninitialized state after construction) may allow UE GC to collect the entry assets during a GC cycle, causing `EntryRegistry` soft refs to resolve to null.

**Fix applied:** `EntryLoadHandle` and `CollectionLoadHandle` are `TSharedPtr<FStreamableHandle>` in this spec. The `RequestSyncLoad` overload returning a `TSharedPtr<FStreamableHandle>` is used.

---

### 8. `GetPage` resolves `UJournalRegistrySubsystem` via `GetWorld()->GetGameInstance()->GetSubsystem<T>()`

**Severity: Low**

Multi-step accessor chain repeated on every `GetPage` call. Fragile and wasteful.

**Fix applied:** `RegistrySubsystem` is cached as a `UPROPERTY()` member in `UJournalComponent`, resolved once in `BeginPlay`.

---

### 9. No `EndPlay` / Handle Cleanup Required for Journal Component

**Observation (not a bug):** `UJournalComponent` does not subscribe to any Event Bus channels itself — it only broadcasts. Therefore it has no `FGameplayMessageListenerHandle` to clean up. The bridge component in the game module is responsible for its own handle cleanup, which the Usage guide covers correctly.

---

### 10. Server Broadcasts Event Before Confirming Client RPC Was Sent

**Severity: Low / Acceptable**

In `AddEntry`, `Bus->Broadcast(...)` is called after `Client_AddEntry(Handle)`. Because `Client_AddEntry` is a `Reliable` RPC, it is only queued — not guaranteed delivered — at the point of the broadcast. An external system reacting to the event bus message might query the journal component's server state before the client has received its copy.

This is inherent in the client-RPC model and is not a real bug — the server is always the authority and any consumer reacting server-side to the bus message operates on the server's `AcquiredSet`. Client-side UI binds to `OnEntryAdded` delegate directly, not the bus. The ordering is acceptable.

---

## Architectural Assessment

**Good:**
- Entry-as-tag identity is correct and consistent with GameCore patterns.
- `ServerPersistenceBuffer` separation is a clean solution to the "server needs to serialize but not query" problem.
- `UJournalRegistrySubsystem` as a `UGameInstanceSubsystem` is the correct scope — outlives worlds, shared cleanly.
- Collections as derived progress (never stored per-player) is correct and eliminates an entire class of consistency bugs.
- Event Bus integration is the right pattern — journal broadcasts, never subscribes.

**Concerns:**
- `GetPage` performance will become an issue if the journal has 2000+ entries and the UI calls it frequently. Cache invalidation on `OnEntryAdded` is a straightforward optimization for the implementation phase.
- No per-player journal entry **cap**. A malicious or buggy server could grow `ServerPersistenceBuffer` indefinitely. Adding a `MaxEntries` config (per track or total) would prevent unbounded growth and is worth considering at design time.
- `BuildDetails` is a C++ `TFunction` virtual. This works cleanly for C++ subclasses but makes Blueprint-only entry data assets difficult — Blueprint implementations of `BuildDetails` require a different approach (Blueprint-callable event + latent node). For a pirate MMORPG where most entry types will be content-authored by designers in Blueprint, this may be a real limitation worth addressing before implementation.
