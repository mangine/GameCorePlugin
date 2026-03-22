# Journal System — Implementation Deviations

## Summary of Deviations from Spec

---

### DEV-J-1: `IPersistableComponent::IsDirty()` added

**Spec:** The spec does not mention `IsDirty()` on `IPersistableComponent`.
**Actual interface (PersistableComponent.h):** Defines `virtual bool IsDirty() const = 0;`
**Resolution:** `UJournalComponent` implements `IsDirty() const override { return bDirty; }` as required by the actual interface.

---

### DEV-J-2: `LogJournal` log category definition placement

**Spec:** References `UE_LOG(LogJournal, ...)` without specifying where the category is declared.
**Resolution:** `DECLARE_LOG_CATEGORY_EXTERN(LogJournal, Log, All)` added to `JournalComponent.h`; `DEFINE_LOG_CATEGORY(LogJournal)` in `JournalComponent.cpp`. The registry subsystem uses `LogTemp` for its load messages to avoid adding a second category header dependency.

---

### DEV-J-3: `TAG_GameCoreEvent_Journal_EntryAdded` defined inline

**Spec:** References `TAG_GameCoreEvent_Journal_EntryAdded` as a native tag handle without specifying the declaration location.
**Resolution:** Declared with `UE_DEFINE_GAMEPLAY_TAG_STATIC` inside `JournalComponent.cpp`. This keeps it local to the translation unit that uses it. The tag string `"GameCoreEvent.Journal.EntryAdded"` must be registered in `DefaultGameplayTags.ini` as described in Architecture.md.

---

### DEV-J-4: `FStreamableHandle` wrapping in `LoadAllEntryAssets` / `LoadAllCollections`

**Spec:** `EntryLoadHandle = MakeShareable(UAssetManager::Get().GetStreamableManager().RequestSyncLoad(Paths).Get())`
**Issue:** `RequestSyncLoad` returns a `TSharedPtr<FStreamableHandle>`. Calling `.Get()` on that and then `MakeShareable` on the raw pointer would double-manage the same object.
**Resolution:** Stored the `TSharedPtr<FStreamableHandle>` returned by `RequestSyncLoad` directly: `EntryLoadHandle = UAssetManager::Get().GetStreamableManager().RequestSyncLoad(Paths)`. This is the correct ownership pattern.

---

### DEV-J-5: Empty path guards in `LoadAllEntryAssets` / `LoadAllCollections`

**Spec:** Calls `RequestSyncLoad` unconditionally.
**Resolution:** Added an `if (Paths.Num() > 0)` guard before `RequestSyncLoad` to avoid an empty-array sync load (which is a no-op but produces a log warning in some UE builds).

---

### DEV-J-6: `Serialize_Load` takes `uint32 SavedVersion` (Known Issue #6)

**Spec (Architecture Known Issues #6):** "Align to the v2 interface: `Serialize_Load` takes `uint32 SavedVersion`."
**Resolution:** Implemented as `Serialize_Load(FArchive& Ar, uint32 SavedVersion)` matching the actual `IPersistableComponent` interface in `PersistableComponent.h`. The `SavedVersion` parameter is present but not yet used (V1 initial schema).

---

### DEV-J-7: `FRequirementContext` does not have a `ForActor` factory

**Spec (Architecture):** References `FRequirementContext::ForActor(...)` in the roll context construction.
**Actual `RequirementContext.h`:** Only provides a `Make<T>(const T&)` template factory. There is no `ForActor` static method.
**Resolution:** The Journal System does not use `FRequirementContext` directly (no requirements on journal entries). This deviation only affects the Loot System — see `DEVIATIONS.md` in `LootTable/`.

---

### DEV-J-8: Known Issue #1 — GetPage allocates full filtered copy

**Status:** Acknowledged. `GetPage` and `GetFilteredCount` both call `GetFiltered`, each producing an independent sorted copy. For large journals this is O(N log N) per call.
**Not fixed:** The cache-invalidation pattern described in Known Issue #1 is not implemented. Deferring to game module optimization.

---

### DEV-J-9: Known Issue #3 — Synchronous load at init

**Status:** Acknowledged. `LoadAllEntryAssets` and `LoadAllCollections` both use synchronous `RequestSyncLoad`. For large asset sets this will produce a startup hitch.
**Not fixed:** Async load pattern with readiness callback deferred to game module. The spec documents this as a known limitation.

---

### DEV-J-10: `GetEntryTitle` has no default implementation on `UJournalEntryDataAsset`

**Spec:** "GetEntryTitle has no default — concrete subclasses must implement."
**Resolution:** No default `GetEntryTitle_Implementation` is provided. Concrete game-module subclasses must provide it. This matches the spec intent — an abstract base that forces subclasses to supply the title.
