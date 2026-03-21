# Inventory System — Code Review

**Reviewer note:** This review covers design flaws, bad architecture choices, missing features, and implementation risks found in the original specification. Each issue includes a recommended fix.

---

## Issues

### 1. `FInventoryConstraintViolatedMessage` broadcast location is wrong

**Severity: High**

The original GMS Events doc states the broadcast origin is `TryPlaceAt / TryPlaceAuto`. Both methods are callable on the client for local UI prediction. Broadcasting a `ServerOnly` GMS event from the client would cause a runtime assert or silent no-op depending on Event Bus implementation, and is architecturally incorrect.

**Fix (applied in Specs 2):** The event header now documents that this message must be broadcast by the **game module's server RPC handler** after receiving a blocking result from `TryPlace*`. `TryPlace*` methods themselves never broadcast.

---

### 2. `UWeightConstraint::OnItemAdded/OnItemRemoved` are missing from the original spec

**Severity: High**

The original `Constraints.md` defines `OnItemAdded/OnItemRemoved` as virtual stubs with empty default bodies but never shows the `UWeightConstraint` implementations that actually update `CurrentWeight` and fire `OnCurrentWeightChanged`. Without these, `CurrentWeight` is never incremented, and both the weight check and the `FInventoryWeightChangedMessage` are broken.

**Fix (applied in Specs 2):** Full `OnItemAdded`/`OnItemRemoved` implementations are now included in `Constraints.md`.

---

### 3. `USlotCountConstraint::OnItemAdded/OnItemRemoved` use a fragile re-sync pattern

**Severity: Medium**

The constraint tracks `OccupiedSlots` as a cached counter, but the original spec's `OnItemAdded/OnItemRemoved` never shows how `OccupiedSlots` is incremented/decremented. Synchronising by calling `Inventory.GetOccupiedSlotCount()` at every notification is safe but O(n). More importantly, calling this inside `CommitSlotAdd` means the constraint is re-queried after the slot array has already grown, so the count is always correct — but it re-traverses the array on every add/remove.

**Recommendation:** For inventories over ~100 slots, consider maintaining `OccupiedSlots` as a simple `++`/`--` counter based on whether `CommitSlotAdd` opened a new slot (SlotIndex == NextSlotIndex before increment) vs. stacked onto an existing one. The component already knows this at commit time and can pass a `bool bNewSlot` flag to `OnItemAdded`.

---

### 4. `ExecuteAutoPlace` double-calls `RunConstraints` with inconsistent quantities

**Severity: Medium**

For each candidate slot, `ExecuteAutoPlace` first checks `RunConstraints` for the slot-open case (full `QuantityRemaining`), then again for the weight case (per-slot `Placeable`). The two separate calls can produce inconsistent results if `MaxWeight` is very close to `CurrentWeight + Placeable * ItemWeight`. The slot-open check may pass even though the weight check will immediately fail on the next line, burning a loop iteration.

**Recommendation:** Merge into a single `RunConstraints` call per candidate passing `Placeable` as the quantity, after the `Placeable > 0` guard. The slot-count check (which needs `QuantityRemaining` to decide whether a new slot is needed) can remain separate as a pre-flight before entering the per-candidate loop.

---

### 5. `FInventorySlotArray::PostReplicatedChange` outer resolution is undocumented

**Severity: Medium**

The spec declares `PostReplicatedChange` but does not document how it resolves the owning `UInventoryComponent` to fire `OnInventoryChanged`. This is a well-known FFastArraySerializer footgun: if the implementation tries to cache a raw pointer to the component inside the struct, it will break because the struct can be copied/moved during serialization.

**Fix (applied in Specs 2):** The spec now explicitly states: use `GetTypedOuter<UInventoryComponent>()` inside the implementation. Do not store a raw component pointer inside `FInventorySlotArray`.

---

### 6. `DropItem` and `DismantleItem` do not validate `HasAuthority` in original spec

**Severity: Medium**

The original `UInventoryComponent.md` lists `HasAuthority()` as a requirement in prose but the method bodies shown do not include the guard. In a replicated MMORPG context, calling these on a client would silently corrupt local slot state without replicating it.

**Fix (applied in Specs 2):** Both `DropItem` and `DismantleItem` now show explicit `if (!HasAuthority()) return false;` guards at the top.

---

### 7. `TryPlaceAuto` / `PlaceAuto` share `ExecuteAutoPlace` but differ only in `bDryRun` — no external actor can distinguish them

**Severity: Low**

The pattern of `TryPlaceAuto` (returns a result) followed by `PlaceAuto` (executes it) means the server runs the full placement algorithm twice. For standard bag sizes this is fine, but it means constraint state is read twice and the two results could theoretically diverge if a concurrent mutation occurs between the two calls (e.g. an item expires from another source).

**Recommendation:** For authority paths that do not need the dry-run result for UI purposes, consider providing a combined `TryAndPlace` method that atomically validates and commits. For now the current two-step pattern is acceptable given the server-authoritative model, but this should be documented as a known race window.

---

### 8. `InstanceData` migration is entirely game module responsibility with no guidance

**Severity: Low**

GameCore saves and loads `InstanceData` verbatim. If the game module changes its instance data schema between build versions, stale blobs will be loaded without any error. There is no hook in `Migrate()` for `InstanceData` because GameCore cannot know the schema.

**Recommendation:** Document clearly (done in Specs 2) that the game module must implement its own versioning inside the `InstanceData` blob (e.g. a leading version byte). `IPersistableComponent::Migrate` is only for GameCore-owned fields.

---

### 9. `UTaggedSlotLayout::IndexMap` rebuild is only in `PostLoad` — misses runtime construction

**Severity: Low**

If a `UTaggedSlotLayout` is constructed at runtime (e.g. in C++ via `NewObject`) rather than loaded from a package, `PostLoad` is never called, so `IndexMap` is empty and `TestSlot` always returns `InvalidSlot`.

**Fix:** Override `PostInitProperties` or add an explicit `RebuildIndexMap()` method called from both `PostLoad` and `PostInitProperties`. Alternatively, make `IndexMap` a lazy-built cache: build on first call to `TestSlot` or `GetAutoPlacementCandidates` if empty.

---

### 10. `UInventoryComponent` stores constraints as typed `UPROPERTY` instead of polymorphic array

**Severity: Design Note**

The current design has `TObjectPtr<UWeightConstraint>` and `TObjectPtr<USlotCountConstraint>` as named properties. This is clear and debugger-friendly, but limits extensibility — a game that needs a custom constraint (e.g. `UQuestItemConstraint`) must subclass `UInventoryComponent` or modify GameCore.

**Recommendation (for future):** Consider an additional `TArray<TObjectPtr<UInventoryConstraint>> ExtraConstraints` property that `RunConstraints` also iterates. Named weight/slot properties stay for the common case. This keeps the API ergonomic while allowing game-module extension without touching GameCore.

---

## Summary

| # | Issue | Severity | Fixed in Specs 2? |
|---|---|---|---|
| 1 | ConstraintViolated broadcast in wrong location | High | ✅ Yes |
| 2 | WeightConstraint OnItemAdded/Removed missing | High | ✅ Yes |
| 3 | SlotCountConstraint OccupiedSlots re-sync is O(n) | Medium | ⚠️ Documented |
| 4 | ExecuteAutoPlace double RunConstraints call | Medium | ⚠️ Documented |
| 5 | PostReplicatedChange outer resolution undocumented | Medium | ✅ Yes |
| 6 | DropItem/DismantleItem missing HasAuthority guard | Medium | ✅ Yes |
| 7 | TryPlace + Place double execution race window | Low | ⚠️ Documented |
| 8 | InstanceData migration guidance missing | Low | ✅ Documented |
| 9 | IndexMap not rebuilt on runtime construction | Low | ✅ Noted |
| 10 | No extensible constraint array | Design Note | ⚠️ Future recommendation |
