# Serialization System — Code Review

## Overview

The Serialization System is well-conceived at the architectural level. The responsibility split between `UPersistenceSubsystem` (serialize + dispatch) and `IKeyStorageService` (queue + deliver) is clean and correct. The tag-keyed delegate routing is generic and extensible. The binary format choice is appropriate for MMORPG scale. The generation counter for dirty clearing is a good piece of defensive engineering.

However, there are several real implementation issues that would cause bugs or compilation errors, and a few design weaknesses worth fixing before implementation starts.

---

## Issue 1 — UInterface Cannot Store Instance Data (Bug / Compile Error)

**Severity: High**

The original spec declares `bDirty`, `DirtyGeneration`, and `CachedRegComp` as members of `IPersistableComponent` (the UInterface class). This does not work: UInterface-derived classes cannot hold instance data managed by UObject — doing so causes undefined behavior and potential memory corruption.

**Fix (applied in specs):** These fields must be declared on the **implementing component class** (the `UActorComponent` subclass). The interface declares the contract (`ClearIfSaved`, `GetPersistenceKey`, etc.); the implementing class provides the storage. Each class implementing the interface repeats the boilerplate — acceptable given there will be few such classes per project.

Alternatively, a `TPersistableComponentMixin<T>` CRTP base could remove the boilerplate, but this adds complexity without strong justification at this stage.

---

## Issue 2 — `bDirty` Access in `BuildPayload` (Bug)

**Severity: High**

`UPersistenceRegistrationComponent::BuildPayload` accesses `Persistable->bDirty` directly. Since `bDirty` is not a member of `IPersistableComponent` (it lives on the implementing class), this will not compile against the interface pointer.

**Fix:** Add `virtual bool IsDirty() const = 0;` to `IPersistableComponent`. Implementing classes return `bDirty`. This keeps `BuildPayload` working against the interface without downcasting.

---

## Issue 3 — `MoveToSaveQueue` Does Not Exist (Bug)

**Severity: High**

The original `EndPlay` implementation in `UPersistenceRegistrationComponent` calls `Subsystem->MoveToSaveQueue(...)`, which is not declared anywhere in `UPersistenceSubsystem`. The subsystem has no internal save queue — payloads are dispatched immediately via `DispatchPayload`.

**Fix (applied in specs):** `EndPlay` now calls `Subsystem->RequestFullSave(GetOwner(), ESerializationReason::Logout)` directly. This routes through the existing dispatch path correctly.

---

## Issue 4 — `GetCachedPersistables()` Missing Declaration (Bug)

**Severity: High**

`UPersistenceSubsystem::OnRawPayloadReceived` calls `RegComp->GetCachedPersistables()` but this accessor was not declared in the original `UPersistenceRegistrationComponent` spec.

**Fix (applied in specs):** `GetCachedPersistables()` is now declared as a `const` method returning `const TArray<TScriptInterface<IPersistableComponent>>&`.

---

## Issue 5 — `UPersistenceSubsystem::GetServerInstanceId()` Static vs Instance Member (Bug)

**Severity: Medium**

The original `BuildPayload` in `UPersistenceRegistrationComponent` references `UPersistenceSubsystem::GetServerInstanceId()` as a static call, but `ServerInstanceId` is declared as a `UPROPERTY` instance member on the subsystem.

**Fix (applied in specs):** `BuildPayload` now fetches `ServerInstanceId` via the subsystem instance (`GetGameInstance()->GetSubsystem<UPersistenceSubsystem>()->ServerInstanceId`). If performance is a concern at scale, `ServerInstanceId` can be cached on the registration component during `BeginPlay`.

---

## Issue 6 — No PIE Teardown Guard (Bug)

**Severity: Medium**

The original `EndPlay` had no `EEndPlayReason::EndPlayInEditor` filter. In PIE, every actor's `EndPlay` fires on stop, triggering spurious saves and potentially corrupting real save data if the subsystem accidentally creates in PIE mode.

**Fix (applied in specs):** Both `BeginPlay` (skips registration) and `EndPlay` (early-returns) guard against `EndPlayInEditor`.

---

## Issue 7 — No Guard Against Double Full Cycle Start (Bug)

**Severity: Medium**

If `SaveInterval` is shorter than the time it takes to complete a full cycle across ticks (can happen with large `RegisteredEntities` counts and low `ActorsPerFlushTick`), the next `FlushSaveCycle` call triggers a new full cycle snapshot while the previous one is still running, clobbering `FullCycleEntitySnapshot` and `FullCycleCursorIndex`.

**Fix (applied in specs):** Added `if (bFullCycleInProgress) return;` at the top of the full cycle branch in `FlushSaveCycle`.

---

## Issue 8 — Partial Cycle Does Not Guarantee All Dirty Actors Flush (Design)

**Severity: Low / By Design**

If `DirtySet.Num() > ActorsPerFlushTick`, leftover dirty actors are deferred until the next partial cycle. Worst-case dirty lag is `2 × SaveInterval`. This is acceptable by design (frame budget protection), but should be documented clearly so operators tune `ActorsPerFlushTick` appropriately for their server population.

**Recommendation:** Add a metric/log that emits when the dirty set is larger than `ActorsPerFlushTick` at cycle start, so operators can detect under-tuned configurations in production.

---

## Issue 9 — Load Path Has No Retry (Design Gap)

**Severity: Low**

`OnComplete(false)` fires on failure and timeout. The game module must implement all retry logic. For a transient DB connection blip, a failed player load means a manual retry or a fresh-spawn decision — a hard call to push entirely to the game module.

**Recommendation:** Add an optional retry count parameter to `RequestLoad`, with exponential backoff handled inside the subsystem before `OnComplete(false)` is fired. This is not urgent but belongs in the backlog.

---

## Issue 10 — Linear Blob-to-Component Matching on Load (Performance)

**Severity: Low**

`OnRawPayloadReceived` matches blobs to components via a nested loop: O(blobs × components). For actors with many persistable components (e.g. a ship with 10+), this is quadratic. 

**Recommendation:** Add a `TMap<FName, IPersistableComponent*>` lookup cache to `UPersistenceRegistrationComponent`, built during `BeginPlay`. `OnRawPayloadReceived` then does O(1) lookups per blob.

---

## Issue 11 — `ServerInstanceId` Not Enforced (Design)

**Severity: Low**

An invalid `ServerInstanceId` is logged as an error but the subsystem continues. In production, payloads stamped with an invalid GUID silently break deduplication and audit tooling — a subtle failure that may not surface until rollback is needed.

**Recommendation:** Add `checkf(ServerInstanceId.IsValid(), ...)` in non-shipping builds to make misconfiguration a hard stop during QA. Keep the log-and-continue behavior in shipping builds.

---

## Issue 12 — `EEndPlayReason` Handling Could Use Game-Specific Logout Reason (Design)

**Severity: Low**

The `EndPlay` save currently always uses `ESerializationReason::Logout`. For actor destruction reasons that are not player logouts (e.g. actor streaming out, world partition unload), `Logout` is semantically incorrect — it will mark the payload `bFlushImmediately=true` unnecessarily.

**Recommendation:** Map `EEndPlayReason` to `ESerializationReason` more carefully: `Destroyed` and `RemovedFromWorld` could use `Periodic` or a new `ActorDestroyed` reason without the flush flag, while only `LevelTransition` and explicit logout paths use `Logout`.

---

## Summary Table

| # | Issue | Severity | Status |
|---|---|---|---|
| 1 | UInterface cannot store instance data | High | Fixed in specs |
| 2 | `bDirty` access across interface boundary | High | Requires `IsDirty()` virtual |
| 3 | `MoveToSaveQueue` does not exist | High | Fixed in specs |
| 4 | `GetCachedPersistables()` missing | High | Fixed in specs |
| 5 | `GetServerInstanceId()` static vs instance | Medium | Fixed in specs |
| 6 | No PIE teardown guard | Medium | Fixed in specs |
| 7 | No guard against double full cycle | Medium | Fixed in specs |
| 8 | Partial cycle may not flush all dirty | Low | By design; document + add metric |
| 9 | No load retry | Low | Backlog |
| 10 | Linear blob matching on load | Low | Backlog — add TMap cache |
| 11 | `ServerInstanceId` not enforced | Low | Add `checkf` in non-shipping |
| 12 | `EndPlay` always uses Logout reason | Low | Refine EEndPlayReason mapping |
