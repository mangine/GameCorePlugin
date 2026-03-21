# Journal System — Code Review

---

## Overview

The Journal System is well-conceived at the architectural level. The core decisions — `FGameplayTag` as identity primitive, server-authoritative mutations with client-side history for pagination, one-shot login sync RPC followed by incremental RPCs, and derived-not-stored collection progress — are all correct and reflect solid MMORPG backend thinking.

The issues below are design flaws, missing safety guards, and architectural gaps that would cause real problems in production.

---

## Issues

### 1. `GetPage()` / `GetFilteredCount()` — O(N log N) per call, no caching

**Severity: Medium**

Both functions call `GetFiltered()` internally, which allocates a new `TArray`, filters all `Entries`, and sorts it on every call. For a journal with 500 entries and a UI that calls these on every frame tick or scroll event, this is meaningful GC/CPU pressure.

The original spec acknowledges no caching exists. This is acceptable for a first implementation but must be addressed before shipping.

**Fix:** Cache the filtered+sorted result per `(TrackFilter, CollectionFilter)` pair. Invalidate on `OnEntryAdded`. A `TMap<TTuple<FGameplayTag, FGameplayTag>, TArray<FJournalEntryHandle>>` cache with a dirty flag is sufficient.

---

### 2. `BuildDetails()` cannot be overridden in Blueprint

**Severity: Low-Medium**

`IJournalEntry::BuildDetails()` takes a `TFunction<void(FJournalRenderedDetails)>` — this cannot be a `UFUNCTION` and therefore cannot be overridden in Blueprint. The spec documents this limitation but does not resolve it.

This means all journal content asset types (books, quests, places, NPCs) must be authored in C++. For a content-heavy MMORPG this is a significant authoring bottleneck.

**Fix:** Add a `UFUNCTION(BlueprintNativeEvent)` `BuildDetails_BP()` that returns `FJournalRenderedDetails` synchronously. C++ `BuildDetails()` calls `BuildDetails_BP()` by default and Blueprint authors override the BP event. For async cases (external asset loads), C++ subclasses still override `BuildDetails()` directly.

---

### 3. Startup sync load in `UJournalRegistrySubsystem` will hitch on large content sets

**Severity: Medium (project-scale dependent)**

`LoadAllEntryAssets()` and `LoadAllCollections()` use `RequestSyncLoad` at `GameInstance::Initialize`. With hundreds of entry assets this is fine. With thousands, or if future entry assets embed non-trivial data, this becomes a measurable startup hitch.

The spec justifies sync load as safe because "entry assets are lightweight." This is true now, but is fragile — a developer adding a non-soft `UTexture2D` property to a concrete subclass silently makes this load heavy.

**Fix:** Use `RequestAsyncLoad` with an `OnComplete` callback. Set a `bRegistryReady` flag and guard `GetEntryAsset()` / `GetCollectionProgress()` with an early return (or a ready-check API). Early callers (unlikely — login sync is later) get no-ops and retry on the next frame.

---

### 4. `Client_InitialJournalSync` RPC — no chunking for large journals

**Severity: High for edge cases**

The initial sync RPC sends the full `TArray<FJournalEntryHandle>` in a single reliable RPC. UE reliable RPCs have a practical ~65 KB budget per RPC (with headers/overhead). At 24 bytes per handle:
- 1000 entries ≈ 24 KB → safe
- 3000 entries ≈ 72 KB → risky
- 5000 entries ≈ 120 KB → will silently fail or crash the channel

For a live MMORPG where veteran players accumulate thousands of journal entries, this is a real risk.

**Fix:** Batch the sync into chunks via a sequential RPC pattern. Send `Client_JournalSyncChunk(TArray<FJournalEntryHandle>, bool bIsLastChunk)`. Client accumulates chunks and fires `OnJournalSynced` only when `bIsLastChunk` is true.

---

### 5. `SerializeForSave` / `SerializeFromSave` naming mismatch with `IPersistableComponent` v2

**Severity: High — compile error**

The original spec uses `SerializeForSave(FArchive&)` and `DeserializeFromSave(FArchive&)` naming. The GameCore Specifications 2 `IPersistableComponent` interface uses `Serialize_Save(FArchive&)` and `Serialize_Load(FArchive&, uint32 SavedVersion)`. These will not match the vtable — the implementation will compile but the virtual dispatch will silently not fire, and the journal will never be persisted.

**Fix (already applied in this spec):** Align to `Serialize_Save` / `Serialize_Load(Ar, SavedVersion)` throughout. This is done in the `UJournalComponent` spec above.

---

### 6. `ComputeProgress` and `CollectMemberTags` — no circular reference guard at runtime

**Severity: High — infinite loop / crash**

The original spec's recursive helpers have no visited-set guard. If a circular collection reference slips through `IsDataValid()` (e.g. manually edited asset, cook pipeline issue, version upgrade), the server or client will infinite-loop and crash.

`IsDataValid()` catches this at author-time, but a runtime guard costs nothing and is essential for robustness.

**Fix (already applied in this spec):** Pass `TSet<FGameplayTag>& VisitedCollections` into both recursive helpers and early-return if already visited.

---

### 7. `GetFiltered()` accesses `UJournalRegistrySubsystem` via `GetWorld()->GetGameInstance()->GetSubsystem<>()`

**Severity: Low — robustness**

This chain can return null if called during an edge case (e.g. world tear-down, wrong net role). No null check is present in the original `GetPage()` spec.

**Fix (already applied in this spec):** Guard the registry access with a null check. If registry is null, return an empty collection filter set (which safely passes all entries through as-is).

---

### 8. `ServerPersistenceBuffer` grows unboundedly per session

**Severity: Low — memory concern for very long sessions**

Every `AddEntry` call appends to `ServerPersistenceBuffer`, which is never trimmed during the session. For players with very long sessions and many repeatable entries (daily quests over many days), this buffer could grow large.

Note: this is a real MMORPG scenario — a player who has run 365 daily quests has 365 handles in the buffer (each ≈ 24 bytes = ~8.7 KB). Not catastrophic but worth noting.

**Fix:** For the initial implementation this is acceptable. A future optimization could deduplicate repeatable entries in the buffer (keep only the latest N timestamps per tag) or cap per-tag entries.

---

### 9. `UJournalRegistrySubsystem` is not a `UWorldSubsystem` — it cannot call `UGameCoreEventBus::Get(this)`

**Severity: None — informational**

The registry subsystem does not need to emit events, so this is not an issue. Confirmed correct: `UJournalComponent` (which has a world context) is responsible for all Event Bus calls.

---

### 10. `IJournalEntry::GetEntryTitle()` on server loads the data asset

**Severity: Low — design clarity**

The spec states `BuildDetails()` is client-only but `GetEntryTitle()` is not annotated. The toast notification pattern in Usage.md calls `Asset->GetEntryTitle()` on the client — correct. However, if `AddEntry` callers on the server ever try to call `GetEntryTitle()` (e.g. for logging), they will find the asset in memory (it was sync-loaded). This is fine but should be documented.

**Recommendation:** Add a comment on `IJournalEntry` clarifying that both methods are safe to call on either machine, but `BuildDetails` must never be called on the server.

---

## Summary

| # | Issue | Severity | Fixed in Spec? |
|---|---|---|---|
| 1 | `GetPage()` no caching — O(N log N) per call | Medium | No — documented as Known Issue |
| 2 | `BuildDetails()` not Blueprint-overridable | Low-Med | No — documented as Known Issue |
| 3 | Sync load at subsystem init — potential hitch | Medium | No — documented as Known Issue |
| 4 | `Client_InitialJournalSync` no chunking | High | No — documented as Known Issue |
| 5 | `IPersistableComponent` API name mismatch | **High** | **Yes** — corrected throughout |
| 6 | No runtime circular ref guard in recursion | **High** | **Yes** — `VisitedCollections` set added |
| 7 | Null registry access in `GetFiltered()` | Low | **Yes** — null guard added |
| 8 | `ServerPersistenceBuffer` unbounded growth | Low | No — acceptable for v1 |
| 9 | Registry subsystem event bus — N/A | None | N/A |
| 10 | `GetEntryTitle()` server-safety undocumented | Low | Noted |
