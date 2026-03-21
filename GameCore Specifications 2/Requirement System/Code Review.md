# Requirement System — Code Review

---

## Overview

The Requirement System is well-conceived at the architectural level. Stateless requirements, zero base-layer dependencies, and the `FInstancedStruct`-based context are all correct decisions. The reactive path via `RegisterWatch` + `UGameCoreEventWatcher` is clean and correctly decoupled.

However, several issues accumulated across iterations that need attention before implementation.

---

## Issues Found

### 1. `URequirementLibrary` was declared as `UBlueprintFunctionLibrary` ❌

**Source spec:** `URequirementLibrary 319d261a36cf811cab04cd92452e80a3.md`

The library is explicitly described as *"internal helper, consuming systems never call this directly"*, yet the original spec declared it as `UBlueprintFunctionLibrary`, which:
- Exposes all static methods to Blueprint, contradicting the stated access contract.
- Adds `UCLASS()` overhead for a class that needs no UE reflection.
- Invites misuse from Blueprint callers bypassing the `List->Evaluate()` contract.

**Fix applied:** Spec updated to a plain C++ class with `static` methods and a deleted constructor. No `UCLASS`, no `GENERATED_BODY`.

---

### 2. `URequirementWatchHelper` orphan reference ❌

**Source spec:** Main page architecture diagram references `URequirementWatchHelper` as a standing class. Sub-pages (Watcher System page) correctly state that registration moved to `URequirementList::RegisterWatch` and no separate helper class exists.

This inconsistency would cause confusion at implementation time.

**Fix applied:** `URequirementWatchHelper` removed from all specs. `RegisterWatch` / `UnregisterWatch` live on `URequirementList` as intended.

---

### 3. Usage Guide referenced the stale v1.2 / v1.3 API ❌

**Source spec:** `Usage Guide.md` — Pattern 2 example

The original usage guide showed:
```cpp
AvailabilityList->OnResultChanged.AddUObject(this, &UQuestComponent::OnAvailabilityChanged);
AvailabilityList->Register(GetWorld());
// ...
AvailabilityList->Unregister(GetWorld());
AvailabilityList->OnResultChanged.RemoveAll(this);
```

This is the `URequirementWatcherManager` API from v1.2 that was explicitly removed in v2. The current API uses `RegisterWatch(Owner, Lambda)` returning `FEventWatchHandle`. There is no `OnResultChanged` multicast delegate.

**Fix applied:** Usage guide rewritten to use the correct `RegisterWatch` / `UnregisterWatch` / `FEventWatchHandle` API throughout.

---

### 4. No initial baseline evaluation — documentation gap ⚠️

The reactive path (`RegisterWatch`) only fires when an event arrives *after* registration. If no relevant event fires, the consuming system has no result. The original spec mentioned this as a "known issue" but the usage examples did not show the correct fix.

**Fix applied:** Usage guide explicitly documents calling `Evaluate` imperatively at setup time for a baseline, shown as an optional but recommended step alongside `RegisterWatch`.

---

### 5. Mixed imperative + event-only lists in OR evaluation ⚠️

Consider a `URequirementList` (OR operator) containing:
- `URequirement_MinLevel` (imperative-capable)
- `URequirement_KilledEnemyType` (event-only, always returns Fail from `Evaluate`)

When `RegisterWatch` fires `EvaluateFromEvent` with a `FLevelChangedEvent` payload:
- `URequirement_MinLevel::EvaluateFromEvent` → delegates to `Evaluate` → reads `FLevelChangedEvent` correctly → may pass.
- `URequirement_KilledEnemyType::EvaluateFromEvent` → reads `FLevelChangedEvent` → wrong type → returns Fail.

With OR, this is fine (MinLevel passes → OR passes). But with AND, the event-only requirement fails every time a non-kill event arrives, making the AND list unreachable without a kill event. This is architecturally acceptable only if designers understand it.

**Recommendation:** If a list contains event-only requirements, either:
- Use a dedicated list with only that event-type's requirements, or
- Override `EvaluateFromEvent` to return `Pass` when the context type doesn't match ("not applicable, don't block").

Document at the Data Asset level which evaluation path the list supports. Add a comment convention like a `bReactiveOnly` note in the Description field.

---

### 6. `TSharedPtr<TOptional<bool>> LastResult` in `RegisterWatch` closure — lifetime concern ⚠️

The `LastResult` shared pointer lives inside the lambda captured by `UGameCoreEventWatcher`. If `UnregisterWatch` is not called before the owner is destroyed, the watcher holds a live closure. `UGameCoreEventWatcher` uses `TWeakObjectPtr<const UObject>` for owner tracking, so it will stop dispatching to dead owners — but the `TSharedPtr` and the `TWeakObjectPtr<const URequirementList>` inside the closure are not freed until the watcher's subscription map is cleaned up.

This is not a crash risk (weak pointers guard correctly), but it is a memory leak of the closure allocation until the watcher processes the next event or until it is explicitly unregistered.

**Recommendation:** `UGameCoreEventWatcher::Register` should support owner-lifetime binding so registrations are automatically removed when the owner is garbage collected, without requiring explicit `Unregister` calls. This is a quality-of-life improvement for the Event Bus system.

---

### 7. `ERequirementDataAuthority` enum referenced in user context but absent from specs ⚠️

Session memory indicates `GetDataAuthority()` virtual method was added to `URequirement` during HAP-6 sessions with an `ERequirementDataAuthority` enum. This is not present in any of the specification files in either `GameCore Specifications` or this system's existing sub-pages.

Two possibilities:
1. It was planned but not committed to these specs.
2. It was removed (the Design Decisions page lists it in "Removed Types").

The Design Decisions page confirms it was removed alongside `URequirement_Persisted`. The session memory reference is to a HAP-6 session that may have been rolled back or superseded by the Design Decisions page.

**Resolution:** `GetDataAuthority()` and `ERequirementDataAuthority` are correctly absent. The current design has authority on the list (asset), not on individual requirements. No action needed.

---

### 8. `URequirementList` inherits `UPrimaryDataAsset` — Asset Manager registration required ⚠️

`UPrimaryDataAsset` requires explicit registration in `DefaultGame.ini` (Asset Manager rules) to be loadable by the Asset Manager. Failing to register means `GetPrimaryAssetId()` returns an invalid ID and async loading via Asset Manager will not find these assets.

This is a project-level setup concern, not a code bug, but it must be documented for the implementing developer.

**Recommendation:** Add an Asset Manager configuration note to the Architecture doc or in a project setup guide. If `URequirementList` assets will be referenced directly by hard references from Data Assets (e.g. quest definitions), `UPrimaryDataAsset` is slightly heavier than needed — `UDataAsset` suffices. Keeping `UPrimaryDataAsset` is fine if async loading via the Asset Manager is intended for these lists.

---

## Summary

| Issue | Severity | Status |
|---|---|---|
| `URequirementLibrary` as `UBlueprintFunctionLibrary` | High | Fixed in spec |
| `URequirementWatchHelper` orphan reference | High | Fixed in spec |
| Usage Guide stale API (v1.2 pattern) | High | Fixed in spec |
| No baseline evaluation documentation | Medium | Fixed in spec |
| Mixed imperative + event-only in OR/AND | Medium | Documented as known issue |
| `LastResult` closure memory leak without explicit unregister | Low | Documented, Event Bus improvement recommended |
| `ERequirementDataAuthority` confusion | Low | Confirmed removed, no action |
| `UPrimaryDataAsset` Asset Manager registration | Low | Noted, project-level setup concern |
