# Notification System — Code Review

---

## Overview

The Notification System is well-scoped and correctly positioned as a client-only, presentation-layer consumer. The delegate-driven design is clean, the adapter pattern for channel bindings is sound, and the in-memory-only viewed state decision is correct for a generic plugin. The issues below range from correctness bugs to architectural mismatches with the rest of GameCore.

---

## Issues

### 1. `RegisterChannelListeners` in the Original Spec Used Raw GMS, Not the Event Bus (Correctness)

The original `Subsystem.md` `RegisterChannelListeners` called `UGameplayMessageSubsystem` directly using a raw `FGameplayTag` listener — bypassing the GameCore Event Bus entirely. The `GMS Integration.md` file correctly described using `UGameCoreEventBus2` (now `UGameCoreEventSubsystem`). These two files contradicted each other.

**Fixed in V2**: All listener registration goes through `UGameCoreEventSubsystem`. The `RegisterListener` virtual on `UNotificationChannelBinding` receives a `UGameCoreEventSubsystem*`, not a raw GMS pointer. The subsystem only calls `UGameCoreEventSubsystem::Get(this)` — it never touches GMS directly.

---

### 2. `HandleIncomingEntry` Was in `private` But Needed to Be `public` (Correctness)

The original `Subsystem.md` declared `HandleIncomingEntry` in the `private` section. The `GMS Integration.md` file correctly noted it must be `public` so bindings can call it. A binding in a game module cannot call a private method on the subsystem.

**Fixed in V2**: `HandleIncomingEntry` is `public` with a clear doc comment: "Public to allow binding access. Not intended as a game-layer API — use `PushNotification` for direct pushes."

---

### 3. `DismissNotification` Had a Dangling Pointer Risk (Correctness)

The original `DismissNotification` called `Group->Entries.RemoveAll(...)`, then `Groups.Remove(Group->CategoryTag)` while `Group` was still a raw pointer into the `TMap`. After the `RemoveAll`, if the group entry was removed from the TMap, the pointer was potentially dangling on the `Groups.Remove` call. The issue was subtle because `Groups.Remove` was called after `Group` had already been used — but the pointer validity window was narrow and depended on execution order.

Additionally, the original decremented `Group->UnviewedCount` and `CachedTotalUnviewed` **after** the `RemoveAll`, meaning it was decrementing based on a field of an entry that had already been removed.

**Fixed in V2**: `CategoryTag` is cached before any mutation. `bWasUnviewed` is captured before `RemoveAll`. `Groups.Remove` is called only after `OnGroupChanged` fires with the still-valid group reference.

---

### 4. `CachedTotalUnviewed` Has No Self-Healing Path (Design Risk)

The write-through counter `CachedTotalUnviewed` must be correctly updated by every mutating code path. There is no periodic recalculation or validation. A single missed decrement (e.g. in a future added code path) produces a silent badge desync that is hard to reproduce.

**Suggestion**: Add a `RecalculateTotalUnviewed()` private method that counts directly from `Groups` and returns the true value. Call it in debug builds (via `ensure(CachedTotalUnviewed == RecalculateTotalUnviewed())`) after every mutation in `PushEntryInternal`, `MarkViewed`, `MarkCategoryViewed`, `MarkAllViewed`, `DismissNotification`, and `DismissCategory`. This catches desyncs immediately in development without touching shipping builds.

```cpp
int32 UGameCoreNotificationSubsystem::RecalculateTotalUnviewed() const
{
    int32 Total = 0;
    for (const auto& [Tag, Group] : Groups)
        Total += Group.UnviewedCount;
    return Total;
}
```

---

### 5. `DismissCategory` Does Not Fire `OnGroupChanged` (Missing Delegate)

The original `DismissCategory` removes the entire group and fires `OnAllViewed` if total is zero, but never fires `OnGroupChanged`. UI widgets bound to a specific category's group badge will not update when the category is dismissed wholesale.

**Fixed in V2**: `DismissCategory` is not changed in the spec because `OnGroupChanged` for a removed group is semantically awkward (the group no longer exists). The correct fix is for UI widgets to also bind `OnGroupChanged` per-category AND handle the case where `GetGroup(CategoryTag)` returns an empty group (UnviewedCount == 0, Entries empty). Documenting this expectation in the spec is sufficient.

**Suggestion**: Add a dedicated `OnCategoryDismissed` delegate that fires from `DismissCategory` with the dismissed `FGameplayTag`. This is cleaner than having UI infer dismissal from an empty `GetGroup` result.

---

### 6. No Validation for Duplicate Channel Tags in `UNotificationChannelConfig` (Data Integrity)

Two bindings with the same `Channel` tag register two listeners for the same channel, producing duplicate notifications silently.

**Suggestion**: Add `IsDataValid` to `UNotificationChannelConfig`:

```cpp
EDataValidationResult UNotificationChannelConfig::IsDataValid(
    FDataValidationContext& Context) const
{
    TSet<FGameplayTag> SeenChannels;
    for (const UNotificationChannelBinding* Binding : Bindings)
    {
        if (!Binding) continue;
        if (SeenChannels.Contains(Binding->Channel))
        {
            Context.AddError(FText::Format(
                LOCTEXT("DuplicateChannel", "Duplicate channel tag {0} in Bindings."),
                FText::FromString(Binding->Channel.ToString())));
        }
        SeenChannels.Add(Binding->Channel);
    }
    return SeenChannels.Num() == Bindings.Num()
        ? EDataValidationResult::Valid
        : EDataValidationResult::Invalid;
}
```

---

### 7. `UNotificationChannelConfig::UnregisterChannelListeners` Does Not Match Per-Binding Handle Ownership (Design Smell)

All listener handles are stored in a flat `TArray<FGameplayMessageListenerHandle> ListenerHandles` on the subsystem. `UnregisterChannelListeners` passes this array to each binding's `UnregisterListeners` call, which empties it. If a binding registered more than one handle, or handles are interleaved from multiple bindings, the per-binding `UnregisterListeners` call receives handles it did not create.

**Suggestion**: Store handles per-binding in a `TMap<UNotificationChannelBinding*, TArray<FGameplayMessageListenerHandle>>`. Each binding registers into its own handle array; `UnregisterListeners` is called with the correct per-binding array. This eliminates handle ownership ambiguity.

---

### 8. `GetAllGroups` Returns by Value with No Ordering Guarantee (Minor API Gap)

`GetAllGroups()` returns a copy of all groups with no defined ordering. The spec comment says "No guaranteed ordering — UI should sort by `Entries.Last().Timestamp`". This is correct but relies on the caller knowing this. An empty group (no entries) would crash on `Entries.Last()`.

**Suggestion**: Groups with no entries should not exist in `Groups` (the current `DismissNotification` removes empty groups). Add an `ensure(!Group.Entries.IsEmpty())` in `GetAllGroups` in debug builds to enforce this invariant.

---

### 9. Session-Restore Viewed State Pattern Is Fundamentally Broken (Design Flaw)

The original spec describes a session-restore pattern where the game saves viewed GUIDs and replays `MarkViewed` on load. This is documented as working but is broken by design: GUIDs are generated fresh at push time, so a notification created in session A has a different GUID in session B. Saving and replaying GUIDs across sessions will never match.

The pattern only works within a single session (e.g. closing and reopening the notification panel in the same play session). True cross-session persistence would require stable deterministic IDs (e.g. based on the source event tag + timestamp) — a significant design change.

**Recommendation**: Remove the cross-session restore guidance from the spec. Document clearly: "Viewed state is transient. GUIDs are ephemeral and not stable across sessions. Cross-session persistence of viewed state is not supported and requires game-layer redesign with stable notification IDs."

---

## Non-Issue Suggestions

**Per-entry `OnDismissed` delegate**: currently `DismissNotification` fires `OnGroupChanged` but there is no notification that a specific entry was explicitly dismissed (vs. expired). UI that shows a dismiss animation needs to distinguish "user dismissed" from "expired". Adding `OnNotificationDismissed(FGuid)` fired from `DismissNotification` (but not from `OnEntryExpired`) covers this.

**`PushNotification` validation**: currently if `Entry.CategoryTag.IsValid() == false`, `HandleIncomingEntry` logs a warning and returns. For the direct-push path (`PushNotification`), this is a caller error that should `ensure(Entry.CategoryTag.IsValid())` in non-shipping builds in addition to the log.

**`MaxStackCount` of 0 in category rules creates unbounded memory growth**: unconfigured categories (using default rule) accumulate entries forever. For a notifications-as-toasts design this is fine. If any category is used as a persistent record, it will grow without bound. A global safety cap (e.g. 1000 entries per group max, regardless of category config) would prevent accidental memory bloat.
