# GameCore Backend — Code Review

---

## Overview

The Backend system is architecturally sound and demonstrates strong design discipline: the static facade pattern removes subsystem boilerplate from every call site, the null-fallback contract means systems never crash on missing infrastructure, and the separation of write-behind queue ownership from `UPersistenceSubsystem` is the correct division of responsibility. Tag-based routing is well thought-out and correctly decouples plugin systems from backend topology.

That said, several issues were found — ranging from API inconsistencies to structural risks.

---

## Issues

### 1. `IQueryStorageService` callback signature inconsistency

**Severity: Medium**

`IKeyStorageService` uses `EStorageRequestResult` on all async callbacks. `IQueryStorageService` uses `bool bSuccess`. These two storage interfaces should be consistent — callers should not have to handle failures differently depending on which interface they're using.

**Fix:** Change all `IQueryStorageService` callbacks from `bool bSuccess` to `EStorageRequestResult Result`. Applied in this migration.

---

### 2. Config storage gap in `UGameCoreBackendSubsystem`

**Severity: Medium**

The original spec describes `RegisterXxx(Key, Service, Config)` being called before `Super::Init()`, but `Initialize()` calls `Connect(Config)`. There is no private map defined on the subsystem to hold configs between registration and `Initialize`. A developer implementing this from the spec would have a compile error or undefined behavior.

**Fix:** Added explicit `StoredXxxConfigs` private maps to `UGameCoreBackendSubsystem` and documented the contract clearly in the class spec.

---

### 3. `FAuditServiceBase` has no retry — but this is undocumented

**Severity: Medium**

The original spec states "Implement retry logic internally — FAuditServiceBase does not retry on failure" as a note on `DispatchBatch`. This is easy to miss. An implementor who skips retry loses audit events silently on transient network failures.

**Fix:** Elevated this to a prominent `Architecture.md` Known Issue and added a bold callout in `IAuditService.md`.

---

### 4. `FGameCoreBackend` delegate thread safety is not enforced

**Severity: Low-Medium**

The static `TFunction` delegates (`OnLog`, `OnAudit`, `OnPersistenceWrite`) are not protected by any synchronization. The contract says "bind in Init, clear in Shutdown, never rebind mid-session" — but this is a documentation contract, not a code contract. A developer who violates it has an undetected data race.

**Suggestion:** In a future pass, either wrap delegate reads/writes in a lightweight spinlock, or assert `IsInGameThread()` on any bind/clear operation to catch violations in dev builds.

---

### 5. `FNullKeyStorageService` logs on every write — excessive for high-frequency systems

**Severity: Low**

`FNullKeyStorageService::Set()` emits `UE_LOG Warning` on every call. In a PIE session without a backend, this will spam the log for every autosave tick of `UPersistenceSubsystem`. This is not the correct null behavior for write operations — only reads should log (because a failing read might block player progress).

**Fix:** Change null fallback writes (`Set`, `SetWithTTL`, `BatchSet`, `Delete`, `BatchDelete`) to silent no-ops, and emit a single `UE_LOG Warning` once at first call (e.g. via a `bool bWarnedOnce` flag). Reads (`Get`, `BatchGet`, `ExecuteFunction`) should continue to log per-call since they return failure results that callers will act on.

---

### 6. `ShouldCreateSubsystem` — `GetWorld()` may return null during test environments

**Severity: Low**

The current implementation guards with `if (!World) return false`. This is correct. However, it also means the subsystem silently does not create in automation tests that use a `GameInstance` without a proper World. If test coverage for backend wiring is ever needed, this will be a subtle failure mode. No action needed now — document as a known edge case.

---

### 7. No service health visibility at the subsystem level

**Severity: Low**

Once services are connected, the subsystem has no way to detect that a service has silently failed (e.g. Redis connection dropped). Routing continues to the service as if it were live. The service's internal reconnect logic handles this, but if reconnect fails permanently, the subsystem continues routing to a broken service with no visibility to the caller.

**Suggestion:** Add an optional `IsHealthy()` virtual to service interfaces (default return `true`). `UGameCoreBackendSubsystem::GetXxx()` can check health and fall back to the null implementation if unhealthy. This gives the subsystem visibility and makes failure detectable.

---

### 8. `FGameCoreBackend::PersistenceWrite` is a narrow convenience method

**Severity: Design Note**

`PersistenceWrite` on the facade is a thin wrapper that directly calls `IKeyStorageService::Set`. It does not set `bCritical` or `bFlushImmediately` — the caller would need to use `GetKeyStorage(Tag)->Set(...)` directly for those flags. This makes the canonical method less useful for the primary use case (logout saves need `bCritical = true`).

**Suggestion:** Either remove `PersistenceWrite` in favor of direct `GetKeyStorage(Tag)->Set(...)` at call sites, or add `bFlushImmediately` and `bCritical` parameters to the method signature.

---

### 9. Logging service uses `Initialize()` not `Connect()` — naming inconsistency

**Severity: Design Note**

Three of the four service interfaces use `Connect(...)` as their setup method. `ILoggingService` uses `Initialize(...)`. This makes the subsystem's connection logic asymmetric — `ConnectedLogging` is populated unconditionally after `Initialize`, while other services require `Connect()` to return `true`.

**Suggestion:** Align `ILoggingService` to use `Connect(const FLoggingConfig& Config)` returning `bool`, consistent with the other three interfaces. This simplifies the subsystem's initialization loop.

---

### 10. `FAuditEntry::ActorDisplayName` is a potential PII risk

**Severity: Design Note**

`ActorDisplayName` is stored in the audit record and dispatched to the backend. Player display names are PII (Personally Identifiable Information). Depending on jurisdiction (GDPR, CCPA), storing player names in audit logs without proper data handling procedures may create compliance obligations.

**Suggestion:** Consider removing `ActorDisplayName` from `FAuditEntry` and having CS tools perform the GUID → display name lookup separately via a player registry API. Alternatively, document this field as requiring PII handling compliance review.

---

## Summary

| # | Severity | Issue | Fixed in Migration? |
|---|---|---|---|
| 1 | Medium | `IQueryStorageService` callback uses `bool`, not `EStorageRequestResult` | ✅ Yes |
| 2 | Medium | Config storage gap between registration and Initialize | ✅ Yes |
| 3 | Medium | `FAuditServiceBase` no-retry undocumented | ✅ Documented |
| 4 | Low-Med | Delegate thread safety not enforced | No — flagged for future |
| 5 | Low | Null write fallbacks log on every call — too noisy | No — flagged |
| 6 | Low | `ShouldCreateSubsystem` null world in test env | No — documented |
| 7 | Low | No service health visibility at subsystem level | No — suggested improvement |
| 8 | Design | `PersistenceWrite` missing `bCritical`/`bFlushImmediately` | No — flagged |
| 9 | Design | `ILoggingService` uses `Initialize` not `Connect` — asymmetric | No — suggested |
| 10 | Design | `ActorDisplayName` is PII | No — flagged for review |
