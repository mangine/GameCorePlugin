# FGameCoreBackend

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Backend/GameCoreBackend.h` / `GameCoreBackend.cpp`
**Type:** Plain C++ struct (static facade)

Static zero-cost facade over `UGameCoreBackendSubsystem`. Provides a single, uniform access pattern for all backend services (logging, key storage, query storage, audit) from **any layer of the codebase** — subsystems, components, utilities — with no context object, no subsystem lookup, and no null check required at the call site.

This is the **canonical way all GameCore systems access backend services**. Direct `GetSubsystem<UGameCoreBackendSubsystem>()` calls are not permitted inside the plugin.

**Using `UGameCoreBackendSubsystem` is entirely optional.** The facade can also be wired via lightweight delegates — no subsystem, no interface implementation required.

---

## Motivation

Without this facade, every system that needs logging must either:
- Hold a pointer to `ILoggingService` (requires wiring/injection)
- Call `GetGameInstance()->GetSubsystem<UGameCoreBackendSubsystem>()` inline (verbose, repeated boilerplate, runtime cost per call)

`FGameCoreBackend` solves this universally. One include, one line, always correct.

With tag-based routing, systems also no longer need to know the `FName` key of the backend instance they should write to — they pass their own `FGameplayTag` and the routing map resolves the correct instance automatically. This applies uniformly to all four service types including logging.

---

## File Location

```
GameCore/
└── Source/
    └── GameCore/
        └── Backend/
            ├── GameCoreBackend.h
            └── GameCoreBackend.cpp
```

---

## Header

Forward declarations only — no heavy includes. This header is designed to be included in any `.cpp` across the plugin with minimal compile cost.

```cpp
// GameCoreBackend.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

// Forward declarations — concrete types only needed in .cpp
class UGameCoreBackendSubsystem;
class ILoggingService;
class IKeyStorageService;
class IQueryStorageService;
class IAuditService;

/**
 * Static facade over UGameCoreBackendSubsystem.
 * Registered automatically by the subsystem on Initialize/Deinitialize.
 * Falls back gracefully when no subsystem is registered:
 *   - Logging   → UE_LOG (FNullLoggingService)
 *   - Audit     → silent no-op (FNullAuditService)
 *   - Storage   → silent no-op writes, failure callbacks on reads
 *
 * Lightweight delegate hooks allow wiring to any custom system without
 * implementing the full service interfaces. Delegates take priority over
 * the subsystem path when bound. Cost when unbound: one null-check branch,
 * predicted-not-taken by the CPU — effectively zero overhead.
 *
 * Tag-based overloads resolve FGameplayTag -> FName via the subsystem's routing maps.
 * Unregistered tags fall back to NAME_None (the default service).
 *
 * Never returns null. Always safe to call, including before subsystem init and during teardown.
 */
struct GAMECORE_API FGameCoreBackend
{
    // --- Subsystem Registration ---
    // Called exclusively by UGameCoreBackendSubsystem::Initialize and ::Deinitialize.
    // Never call directly.
    static void Register  (UGameCoreBackendSubsystem* Subsystem);
    static void Unregister();

    // -------------------------------------------------------------------------
    // Lightweight Delegate Hooks
    // -------------------------------------------------------------------------
    // Optional. Wire these to intercept calls without implementing a full service
    // interface. When bound, the delegate is called INSTEAD of the subsystem path.
    // When unbound (nullptr), the subsystem or null fallback is used.
    //
    // Set from your GameInstance::Init before any system calls into the backend.
    // Clear (set to nullptr) in GameInstance::Shutdown to avoid dangling captures.
    //
    // Logging: always falls back to UE_LOG even when unbound — never fully silent.
    // Audit / Persistence: silent no-op when unbound and no subsystem is registered.

    // (Severity, Category, Message, OptionalPayload)
    static TFunction<void(ELogSeverity, const FString&, const FString&, const FString&)> OnLog;

    // (Entry)
    static TFunction<void(const FAuditEntry&)> OnAudit;

    // (Tag, EntityId, SerializedBytes)
    static TFunction<void(FGameplayTag, FGuid, TArrayView<const uint8>)> OnPersistenceWrite;

    // -------------------------------------------------------------------------
    // Canonical Call Methods (preferred over GetLogging()->Log() etc.)
    // -------------------------------------------------------------------------
    // Use these instead of chaining GetX()->Method(). They route through the
    // delegate hook first, then fall through to the service or null fallback.
    // Logging always reaches UE_LOG at minimum — it is never silently dropped.

    static void Log(
        ELogSeverity   Severity,
        const FString& Category,
        const FString& Message,
        const FString& Payload = FString{});

    static void Audit(const FAuditEntry& Entry);

    static void PersistenceWrite(
        FGameplayTag           Tag,
        FGuid                  EntityId,
        TArrayView<const uint8> Bytes);

    // -------------------------------------------------------------------------
    // Tag-Based Service Accessors (for direct service interface access)
    // -------------------------------------------------------------------------
    // Prefer the canonical call methods above for logging, audit, and persistence.
    // Use these accessors when you need lower-level service interface methods
    // (e.g. Query, GetById, RecordBatch).
    //
    // Resolves FGameplayTag -> FName via the subsystem routing map.
    // Returns the null fallback if the subsystem is not live.
    static ILoggingService*      GetLogging     (FGameplayTag Tag);
    static IKeyStorageService*   GetKeyStorage  (FGameplayTag Tag);
    static IQueryStorageService* GetQueryStorage(FGameplayTag Tag);
    static IAuditService*        GetAudit       (FGameplayTag Tag);

    // -------------------------------------------------------------------------
    // FName-Based Service Accessors (game module wiring code only)
    // -------------------------------------------------------------------------
    static ILoggingService*      GetLogging     (FName Key = NAME_None);
    static IKeyStorageService*   GetKeyStorage  (FName Key = NAME_None);
    static IQueryStorageService* GetQueryStorage(FName Key = NAME_None);
    static IAuditService*        GetAudit       (FName Key = NAME_None);

private:
    // Raw pointer — lifetime is controlled by UGameCoreBackendSubsystem.
    // Unregister() nulls this before the subsystem tears down.
    // Read is always on the game thread; no atomic needed for standard usage.
    static UGameCoreBackendSubsystem* Instance;
};
```

---

## Implementation

```cpp
// GameCoreBackend.cpp
#include "Backend/GameCoreBackend.h"
#include "Backend/BackendSubsystem.h"
#include "Backend/LoggingService.h"
#include "Backend/KeyStorageService.h"
#include "Backend/QueryStorageService.h"
#include "Backend/AuditService.h"

// Static instance
UGameCoreBackendSubsystem* FGameCoreBackend::Instance = nullptr;

// Static delegate hooks — nullptr by default (zero overhead when unbound)
TFunction<void(ELogSeverity, const FString&, const FString&, const FString&)>
    FGameCoreBackend::OnLog            = nullptr;
TFunction<void(const FAuditEntry&)>
    FGameCoreBackend::OnAudit          = nullptr;
TFunction<void(FGameplayTag, FGuid, TArrayView<const uint8>)>
    FGameCoreBackend::OnPersistenceWrite = nullptr;

// Static null fallbacks — constructed once at module load.
static FNullLoggingService      GNullLogging;
static FNullKeyStorageService   GNullKeyStorage;
static FNullQueryStorageService GNullQueryStorage;
static FNullAuditService        GNullAudit;

void FGameCoreBackend::Register(UGameCoreBackendSubsystem* Subsystem)
{
    Instance = Subsystem;
}

void FGameCoreBackend::Unregister()
{
    Instance = nullptr;
}

// --- Canonical call methods ---

void FGameCoreBackend::Log(
    ELogSeverity   Severity,
    const FString& Category,
    const FString& Message,
    const FString& Payload)
{
    if (OnLog)
    {
        OnLog(Severity, Category, Message, Payload);
        return;
    }
    // Falls through to service (which itself falls back to UE_LOG via FNullLoggingService)
    GetLogging(FGameplayTag{})->Log(Severity, Category, Message, Payload);
}

void FGameCoreBackend::Audit(const FAuditEntry& Entry)
{
    if (OnAudit)
    {
        OnAudit(Entry);
        return;
    }
    // Falls through to service — FNullAuditService is a silent no-op when no subsystem
    GetAudit(FGameplayTag{})->RecordEvent(Entry);
}

void FGameCoreBackend::PersistenceWrite(
    FGameplayTag            Tag,
    FGuid                   EntityId,
    TArrayView<const uint8> Bytes)
{
    if (OnPersistenceWrite)
    {
        OnPersistenceWrite(Tag, EntityId, Bytes);
        return;
    }
    // Falls through to service — FNullKeyStorageService silent no-op when no subsystem
    GetKeyStorage(Tag)->Set(Tag, EntityId, TArray<uint8>(Bytes.GetData(), Bytes.Num()), false, false);
}

// --- Tag-based accessors ---

ILoggingService* FGameCoreBackend::GetLogging(FGameplayTag Tag)
{
    if (Instance)
    {
        const FName Key = Instance->ResolveLoggingTag(Tag);
        return Instance->GetLogging(Key);
    }
    return &GNullLogging;
}

IKeyStorageService* FGameCoreBackend::GetKeyStorage(FGameplayTag Tag)
{
    if (Instance)
    {
        const FName Key = Instance->ResolveKeyStorageTag(Tag);
        return Instance->GetKeyStorage(Key);
    }
    return &GNullKeyStorage;
}

IQueryStorageService* FGameCoreBackend::GetQueryStorage(FGameplayTag Tag)
{
    if (Instance)
    {
        const FName Key = Instance->ResolveQueryStorageTag(Tag);
        return Instance->GetQueryStorage(Key);
    }
    return &GNullQueryStorage;
}

IAuditService* FGameCoreBackend::GetAudit(FGameplayTag Tag)
{
    if (Instance)
    {
        const FName Key = Instance->ResolveAuditTag(Tag);
        return Instance->GetAudit(Key);
    }
    return &GNullAudit; // silent no-op
}

// --- FName-based accessors ---

ILoggingService* FGameCoreBackend::GetLogging(FName Key)
{
    return Instance ? Instance->GetLogging(Key) : &GNullLogging;
}

IKeyStorageService* FGameCoreBackend::GetKeyStorage(FName Key)
{
    return Instance ? Instance->GetKeyStorage(Key) : &GNullKeyStorage;
}

IQueryStorageService* FGameCoreBackend::GetQueryStorage(FName Key)
{
    return Instance ? Instance->GetQueryStorage(Key) : &GNullQueryStorage;
}

IAuditService* FGameCoreBackend::GetAudit(FName Key)
{
    return Instance ? Instance->GetAudit(Key) : &GNullAudit;
}
```

---

## Integration with UGameCoreBackendSubsystem

`UGameCoreBackendSubsystem` calls `Register`/`Unregister` automatically. **No manual wiring needed by the game module.**

```cpp
void UGameCoreBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    NullKeyStorage   = MakeUnique<FNullKeyStorageService>();
    NullQueryStorage = MakeUnique<FNullQueryStorageService>();
    NullAudit        = MakeUnique<FNullAuditService>();
    NullLogging      = MakeUnique<FNullLoggingService>();

    FGameCoreBackend::Register(this);
}

void UGameCoreBackendSubsystem::Deinitialize()
{
    for (auto& [Key, Service] : LoggingServices)
        if (ConnectedLogging.Contains(Key))
            Service.GetInterface()->Flush();

    for (auto& [Key, Service] : AuditServices)
        if (ConnectedAudit.Contains(Key))
            Service.GetInterface()->Flush();

    FGameCoreBackend::Unregister();

    KeyStorageServices.Empty();
    QueryStorageServices.Empty();
    AuditServices.Empty();
    LoggingServices.Empty();

    ConnectedKeyStorage.Empty();
    ConnectedQueryStorage.Empty();
    ConnectedAudit.Empty();
    ConnectedLogging.Empty();

    KeyStorageRoutes.Empty();
    QueryStorageRoutes.Empty();
    AuditRoutes.Empty();
    LoggingRoutes.Empty();
}
```

---

## Tag Routing — How It Works

The game module registers tag→name mappings on the subsystem once during `GameInstance::Init`. After that, any plugin system can pass its own tag and get the correct backend instance with no knowledge of the topology.

```
FGameCoreBackend::GetLogging(TAG_Log_Security)
    → Instance->ResolveLoggingTag(TAG_Log_Security)
    → LoggingRoutes.Find(tag) → "SecurityLog"
    → Instance->GetLogging("SecurityLog")
    → returns the dedicated security log sink

FGameCoreBackend::GetLogging(TAG_Log_Unregistered)
    → LoggingRoutes.Find(tag) → not found → NAME_None
    → Instance->GetLogging(NAME_None)
    → returns the default logger
```

**Resolution is exact-match only** — O(1) `TMap` lookup. Each tag that needs non-default routing must be registered explicitly.

---

## Usage — Canonical Call Sites

Systems inside the plugin should use the canonical call methods, not the `GetX()->Method()` chain. This ensures delegate hooks are honoured and the correct null fallback is applied.

```cpp
#include "Backend/GameCoreBackend.h"

// Logging — always reaches UE_LOG at minimum
FGameCoreBackend::Log(ELogSeverity::Warning, TEXT("LevelingComponent"), TEXT("Invalid progression tag"));
FGameCoreBackend::Log(ELogSeverity::Error,   TEXT("PersistenceSubsystem"), TEXT("Flush failed"), PayloadJson);

// Audit — silent no-op if no subsystem and no delegate
FGameCoreBackend::Audit(Entry);

// Persistence write — silent no-op if no subsystem and no delegate
FGameCoreBackend::PersistenceWrite(PersistenceTag, EntityId, SerializedBytes);

// Direct service access — use for service-specific methods (Query, GetById, RecordBatch, etc.)
FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)->Set(TAG_Persistence_Entity_Player, EntityId, Data, false, false);
FGameCoreBackend::GetQueryStorage(TAG_Schema_Market_Listing)->Query(TAG_Schema_Market_Listing, Filter, Callback);
FGameCoreBackend::GetAudit(TAG_Audit_Progression)->RecordBatch(Entries, /*bTransactional=*/true);
```

---

## Null Fallback Behaviour

When `Instance` is null and no delegate hook is bound, each service type has a distinct policy:

| Service | Null Fallback | No-Subsystem Behaviour |
|---|---|---|
| `ILoggingService` | `FNullLoggingService` | Always routes to `UE_LOG(LogGameCore, ...)` — never silent |
| `IAuditService` | `FNullAuditService` | **Silent no-op** — audit is opt-in infrastructure |
| `IKeyStorageService` | `FNullKeyStorageService` | Writes: silent no-op. Reads: `bSuccess=false` callback, no log spam |
| `IQueryStorageService` | `FNullQueryStorageService` | Writes: silent no-op. Reads: `bSuccess=false` callback, no log spam |

See individual service spec pages for null implementation details.

---

## Thread Safety

`Register`/`Unregister` are called on the game thread during subsystem init/deinit. The `Instance` pointer read in each getter is not atomic.

**Safe cases (no action needed):**
- All calls from the game thread (the common case for all current GameCore systems)
- `ILoggingService::Log()` is internally thread-safe (lock-free queue) — calling `GetLogging()` on the game thread and passing the pointer to a background thread is safe for the duration of the subsystem's lifetime

**Unsafe case:**
- Background tasks that outlive `UGameCoreBackendSubsystem::Deinitialize()` and call any `FGameCoreBackend::GetX()` after `Unregister()`. This is a lifecycle bug — tasks must complete or be cancelled before `Deinitialize` returns.

If a future system requires background-safe access after teardown, make `Instance` a `std::atomic<UGameCoreBackendSubsystem*>`.

---

## Notes

- `FGameCoreBackend` has no UObject overhead — it is a plain struct with only static methods and one static pointer.
- The static null fallbacks (`GNullLogging`, etc.) are file-scope statics in `GameCoreBackend.cpp`. They are constructed at module load and never destroyed until module unload — no order-of-destruction issues.
- Tag routing resolution is an O(1) `TMap` lookup — negligible cost.
- Delegate hook null-checks are branch-predicted-not-taken — zero overhead in the common case.
- This struct must **not** be used in client-side code. All backend services are server-only.
- See **Custom Wiring Examples** for how to wire delegates to third-party systems without implementing the full service interfaces.
