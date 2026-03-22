# GameCore Backend — Deviations & Implementation Notes

## DEV-1: Service storage uses raw pointers instead of TScriptInterface

**Spec:** `UGameCoreBackendSubsystem` stores services as `TScriptInterface<IKeyStorageService>` etc.

**Deviation:** Changed to raw `IKeyStorageService*`, `IQueryStorageService*`, `IAuditService*`, `ILoggingService*` pointers in all service maps.

**Reason:** The spec simultaneously defines `FLoggingServiceBase` as a plain C++ class (not a UObject) and expects it to be stored in `TScriptInterface<T>` which requires the object to also be a `UObject`. These two requirements are mutually exclusive. Raw pointers support both UObject-based and plain C++ implementations, which is the intended design (game module provides concrete implementations).

**Impact:** Game module must ensure service lifetimes outlive `UGameCoreBackendSubsystem::Deinitialize()`. If a UObject-based service is desired, it can still be registered via its `IXxxService*` interface pointer.

---

## DEV-2: FAuditServiceBase uses manual flush instead of FTimerHandle

**Spec:** `FAuditServiceBase` has a `FTimerHandle FlushTimerHandle` private member, implying FTimerManager usage.

**Deviation:** `FAuditServiceBase` is a plain C++ class with no UWorld/UObject access. `FTimerHandle` requires a `FTimerManager` which is world-dependent. Instead, `FAuditServiceBase` exposes `Flush()` which must be called periodically by the registering code (or by a dedicated game-side timer). `UGameCoreBackendSubsystem::Deinitialize()` calls `Flush()` on all audit services automatically. Concrete implementors in the game module can use `FTimerManager` if they choose.

**Impact:** Automatic periodic flush is not provided in the base class. Concrete implementations must arrange their own periodic flush mechanism if desired between explicit `Flush()` calls.

---

## DEV-3: FLoggingServiceBase implementation added in AuditService.cpp

**Spec:** All service headers are header-only with no corresponding .cpp.

**Deviation:** `FLoggingServiceBase` implementation (thread management, queue drain, reconnect) and `FAuditServiceBase` implementation are in `AuditService.cpp`. The spec says header-only for service files but `FLoggingServiceBase::Run()` and related methods are non-trivial and depend on platform thread APIs, making header-only impractical without forcing every translation unit that includes `LoggingService.h` to pull in thread implementation details.

**Impact:** None. `AuditService.cpp` is an additional file not in the spec file structure, but required for clean separation.

---

## DEV-4: DEFINE_LOG_CATEGORY(LogGameCore) placed in AuditService.cpp

**Spec:** Does not specify where `DEFINE_LOG_CATEGORY(LogGameCore)` should be defined for the Backend files.

**Deviation:** Defined in `AuditService.cpp` since this is the first Backend .cpp file. The existing `Source/GameCore/Core/Backend/GameCoreBackend.cpp` (the old stub) also defines `DEFINE_LOG_CATEGORY(LogGameCoreAudit)` and `DEFINE_LOG_CATEGORY(LogGameCoreBackend)`. The new Backend system uses `LogGameCore` as the shared category throughout, consistent with the null service implementations in the spec.

---

## DEV-5: ShouldCreateSubsystem includes NM_Standalone

**Spec:** `ShouldCreateSubsystem` checks for `NM_DedicatedServer || NM_ListenServer` only.

**Deviation:** Added `NM_Standalone` to allow backend services to function in single-player/PIE testing scenarios. Without this, PIE testing would always use null fallbacks.

**Impact:** Standalone builds also create the backend subsystem. Can be reverted to server-only if the project requires strict server-only enforcement.

---

## DEV-6: TOptional<FDBSortField> replaced with bool + value pattern

**Spec:** `FDBQueryFilter` uses `TOptional<FDBSortField> Sort` with `UPROPERTY()`.

**Deviation:** Changed to `bool bHasSort` + `FDBSortField Sort` since `TOptional<T>` is not compatible with `UPROPERTY()`.

**Impact:** Query callers must set `bHasSort = true` before populating `Sort`. Functionally equivalent.
