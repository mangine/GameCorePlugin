# GameCore Backend — Architecture

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Backend/`  
**Type:** `UGameInstanceSubsystem` + static facade + service interfaces  

---

## Overview

The GameCore Backend is the **single injection point** for all external backend service integrations in the plugin. It manages named service instances for four categories: key-value storage, structured query storage, audit, and logging. GameCore itself has zero knowledge of concrete backends — Redis, PostgreSQL, Datadog, and similar implementations are provided by the game module.

All plugin systems access backend services exclusively through the **`FGameCoreBackend` static facade** — never by querying `UGameCoreBackendSubsystem` directly. Game module wiring code registers services and tag routes on the subsystem once during `GameInstance::Init`.

---

## Dependencies

### Unreal Engine Modules
- `Core`
- `CoreUObject`
- `Engine`
- `GameplayTags`

### GameCore Plugin Systems
- **None.** The Backend is the lowest-level system — it has no runtime dependency on any other GameCore system. Other systems depend on it, not the reverse.

### Runtime Dependencies (external — game module provided)
- Any concrete `IKeyStorageService`, `IQueryStorageService`, `IAuditService`, `ILoggingService` implementation lives in the game module or a dedicated backend plugin.

---

## Requirements

- Server-only: subsystem must not be created on clients.
- All service accessors must never return `nullptr` — null implementations handle graceful degradation.
- Logging null fallback must always reach `UE_LOG`; it is never silently dropped.
- Audit null fallback is a silent no-op — audit is opt-in infrastructure.
- Storage null fallbacks return failure callbacks on reads; writes are silent no-ops.
- Plugin systems must never know the `FName` key of any backend instance — tag routing resolves this transparently.
- Async callbacks always fire (Success / Failure / Cancelled / TimedOut); callers never handle "callback never arrives."
- Calling thread must never block on any I/O operation.
- Services must be registered **before** `Super::Init()` returns in `GameInstance::Init`.
- Graceful shutdown: all audit and logging services are flushed before map cleanup.

---

## Features

- **Four service types:** logging, key-value storage, structured query storage, audit.
- **Multiple named instances per type:** `"PlayerDB"`, `"EconomyDB"`, `"Security"` etc., each routed to a distinct backend.
- **Tag-based routing:** plugin systems pass `FGameplayTag`; the subsystem resolves tag → `FName` instance transparently.
- **Static facade (`FGameCoreBackend`):** zero-cost single-include access from any layer with no context object required.
- **Delegate hooks:** lightweight `TFunction` slots on `FGameCoreBackend` allow wiring to any custom system without implementing full service interfaces.
- **Write-behind queuing:** `IKeyStorageService` and `ILoggingService` own internal queues with priority lanes, pressure flushes, and configurable batch sizes.
- **GUID cancellation tokens:** async reads return a `FGuid` handle; `Cancel(FGuid)` suppresses the callback.
- **Abstract base classes** `FAuditServiceBase` and `FLoggingServiceBase` reduce implementor boilerplate to two overrides each.
- **`FAuditPayloadBuilder`:** type-safe JSON builder for audit payloads.
- **Server-side function execution** on both storage interfaces for atomic/complex backend operations.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| Static facade over subsystem pointer | Zero boilerplate at call sites; no subsystem lookup per call; works before World exists |
| Tag routing on subsystem, FName internal | Plugin systems remain topology-agnostic; game module controls routing in one place |
| Exact-match tag resolution (no hierarchy) | O(1) lookup; explicit registration avoids surprising implicit fallbacks |
| Delegate hooks take priority over subsystem | Enables lightweight wiring without full interface implementation |
| `FNullAuditService` is silent no-op | Audit is opt-in; log spam when unconfigured is undesirable |
| `FNullLoggingService` routes to UE_LOG | Log output must always be reachable in development |
| Write-behind queue owned by service, not subsystem | `UPersistenceSubsystem` needs no queue of its own; concerns separated cleanly |
| `bCritical` priority lane never dropped | Logout / zone transfer saves must survive queue overflow |
| `FGuid` cancellation tokens | Safer than raw pointer or integer — no aliasing risk |
| `FAuditServiceBase` stamps InstanceGUID/ServerId/Timestamp | Callers cannot forge or omit audit identity fields |
| `ShouldCreateSubsystem` server-only | Backend services are server-side only; subsystem must never exist on clients |
| `friend` on service interfaces for `Connect()` | `Connect()` must only be called by the subsystem; `friend` enforces this at compile time |

---

## Logic Flow

### Startup
```
GameInstance::Init()
  └─ RegisterXxxService(FName, Service, Config)   [game module]
  └─ MapTagToXxx(FGameplayTag, FName)              [game module]
  └─ Super::Init()
       └─ UGameCoreBackendSubsystem::Initialize()
            ├─ Constructs null fallbacks
            ├─ Calls Service->Connect() for each registered service
            ├─ Marks connected services in ConnectedXxx sets
            └─ Calls FGameCoreBackend::Register(this)
```

> **Note:** Services are registered on the subsystem **before** `Super::Init()`. The subsystem's `Initialize` is called during `Super::Init()`, at which point it connects services and registers the facade.

### Plugin System Call
```
FGameCoreBackend::Log(Severity, Category, Message)
  ├─ OnLog bound?  → delegate fires, return
  └─ Instance != null?
       ├─ Yes → Instance->ResolveLoggingTag(FGameplayTag{}) → NAME_None
       │         → Instance->GetLogging(NAME_None)
       │         → returns concrete service (or NullLogging if not connected)
       └─ No  → returns &GNullLogging (file-scope static)

FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)
  └─ Instance->ResolveKeyStorageTag(tag) → "PlayerDB"
  └─ Instance->GetKeyStorage("PlayerDB")
       ├─ Registered + connected → returns concrete IKeyStorageService*
       └─ Not registered or not connected → returns NullKeyStorage.Get()
```

### Async Read with Cancellation
```
FGuid Handle = Storage->Get(Tag, Key, Callback)
  └─ Service enqueues async read, returns FGuid handle

[Later, if object destroyed]
Storage->Cancel(Handle)
  └─ Callback fires with EStorageRequestResult::Cancelled
  └─ Object can be safely destroyed
```

### Shutdown
```
UGameCoreBackendSubsystem::Deinitialize()
  ├─ Flush() all connected logging services
  ├─ Flush() all connected audit services
  ├─ FGameCoreBackend::Unregister()  ← GetX() now routes to file-scope null statics
  └─ Clear all service maps and route maps
```

---

## Known Issues

1. **`Connect()` called during `Initialize`, not during registration** — if a service is registered after `Initialize` has completed, `Connect()` is never called. This is an edge case (registration must precede `Super::Init()`), but there is no runtime assertion to catch it.

2. **No health-check / reconnect orchestration at subsystem level** — reconnect is entirely the service implementation's responsibility. If a service silently fails to reconnect, the subsystem has no visibility and continues routing to what it thinks is a connected service.

3. **`IQueryStorageService` uses `bool bSuccess`** on callbacks, not `EStorageRequestResult` — inconsistent with `IKeyStorageService`. A future pass should align both to `EStorageRequestResult`.

4. **Delegate thread safety** — `FGameCoreBackend` static delegates are not guarded by a mutex. Rebinding mid-session while background threads may be invoking them is a data race. The contract (bind in `Init`, clear in `Shutdown`) mitigates this in practice but is not enforced.

5. **Tag routing is exact-match only** — parent-tag fallback (e.g. `Audit.Progression` catching all `Audit.Progression.*` children) is not supported. Every tag that needs non-default routing must be registered explicitly. This is intentional but worth documenting as a gotcha.

6. **`FAuditServiceBase::DispatchBatch` retry is the implementor's responsibility** — the base class has no retry. If a concrete implementation omits retry, events are silently lost on transient failure.

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Backend/
            ├── GameCoreBackend.h          ← FGameCoreBackend static facade (header)
            ├── GameCoreBackend.cpp        ← FGameCoreBackend impl + file-scope null statics
            ├── BackendSubsystem.h         ← UGameCoreBackendSubsystem declaration
            ├── BackendSubsystem.cpp       ← UGameCoreBackendSubsystem implementation
            ├── KeyStorageService.h        ← IKeyStorageService + FKeyStorageConfig
            │                                + EStorageRequestResult + FNullKeyStorageService
            ├── QueryStorageService.h      ← IQueryStorageService + FDBQueryFilter
            │                                + FDBQueryResult + FNullQueryStorageService
            ├── AuditService.h             ← IAuditService + FAuditEntry + FAuditEntryInternal
            │                                + FAuditServiceBase + FNullAuditService
            │                                + FAuditPayloadBuilder
            └── LoggingService.h           ← ILoggingService + FLoggingConfig + ELogSeverity
                                             + FLoggingServiceBase + FLogEntry
                                             + FNullLoggingService
```

> All interfaces, config structs, null fallbacks, and base classes live in headers only — no `.cpp` per service. Concrete implementations belong in the game module or a separate backend plugin.
