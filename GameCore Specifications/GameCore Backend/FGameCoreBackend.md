# FGameCoreBackend

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Backend/GameCoreBackend.h` / `GameCoreBackend.cpp`
**Type:** Plain C++ struct (static facade)

Static zero-cost facade over `UGameCoreBackendSubsystem`. Provides a single, uniform access pattern for all backend services (logging, key storage, query storage, audit) from **any layer of the codebase** — subsystems, components, utilities — with no context object, no subsystem lookup, and no null check required at the call site.

This is the **canonical way all GameCore systems access backend services**. Direct `GetSubsystem<UGameCoreBackendSubsystem>()` calls are not permitted inside the plugin.

---

## Motivation

Without this facade, every system that needs logging must either:
- Hold a pointer to `ILoggingService` (requires wiring/injection)
- Call `GetGameInstance()->GetSubsystem<UGameCoreBackendSubsystem>()` inline (verbose, repeated boilerplate, runtime cost per call)

`FGameCoreBackend` solves this universally. One include, one line, always correct.

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

// Forward declarations — concrete types only needed in .cpp
class UGameCoreBackendSubsystem;
class ILoggingService;
class IKeyStorageService;
class IQueryStorageService;
class IAuditService;

/**
 * Static facade over UGameCoreBackendSubsystem.
 * Registered automatically by the subsystem on Initialize/Deinitialize.
 * Falls back to null implementations (UE_LOG routing) when the subsystem is not live.
 *
 * Usage:
 *   FGameCoreBackend::GetLogging()->LogWarning(TEXT("MySystem"), Message);
 *   FGameCoreBackend::GetKeyStorage(TEXT("PlayerDB"))->Set(Tag, Id, Data, false, false);
 *
 * Never returns null. Always safe to call, including before subsystem init and during teardown.
 */
struct GAMECORE_API FGameCoreBackend
{
    // --- Registration ---
    // Called exclusively by UGameCoreBackendSubsystem::Initialize and ::Deinitialize.
    // Never call directly.
    static void Register  (UGameCoreBackendSubsystem* Subsystem);
    static void Unregister();

    // --- Service Accessors ---
    // Returns the live service for the given key, or the static null fallback.
    // Null fallbacks route all calls to UE_LOG — see Null Fallback Behavior below.
    static ILoggingService*      GetLogging     (FName Key = NAME_None);
    static IKeyStorageService*   GetKeyStorage  (FName Key = NAME_None);
    static IQueryStorageService* GetQueryStorage(FName Key = NAME_None);
    static IAuditService*        GetAudit       (FName Key = NAME_None);

private:
    // Raw pointer — lifetime is controlled by UGameCoreBackendSubsystem.
    // Unregister() nulls this before the subsystem tears down.
    // Read is always on the game thread; no atomic needed for standard usage.
    // See Thread Safety note below for background thread considerations.
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

// Static instance — set by UGameCoreBackendSubsystem::Initialize
UGameCoreBackendSubsystem* FGameCoreBackend::Instance = nullptr;

// Static null fallbacks — constructed once at module load, never destroyed until module unload.
// These are the UE_LOG-routing implementations used whenever no live service is registered.
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
    FGameCoreBackend::Register(this); // ← facade goes live
}

void UGameCoreBackendSubsystem::Deinitialize()
{
    // 1. Flush all buffered services first
    for (auto& [Key, Service] : LoggingServices)
        if (ConnectedLogging.Contains(Key))
            Service.GetInterface()->Flush();

    for (auto& [Key, Service] : AuditServices)
        if (ConnectedAudit.Contains(Key))
            Service.GetInterface()->Flush();

    // 2. Unregister facade BEFORE clearing maps — any log calls during cleanup still resolve
    FGameCoreBackend::Unregister(); // ← facade drops to null fallbacks

    // 3. Clear all service maps
    KeyStorageServices.Empty();
    QueryStorageServices.Empty();
    AuditServices.Empty();
    LoggingServices.Empty();

    ConnectedKeyStorage.Empty();
    ConnectedQueryStorage.Empty();
    ConnectedAudit.Empty();
    ConnectedLogging.Empty();
}
```

**Order matters:** Flush → Unregister → Empty maps. This guarantees any `UE_LOG` or logging calls made during map teardown still resolve safely to null fallbacks rather than crashing on dangling pointers.

---

## Usage — Call Site Pattern

All GameCore systems (subsystems, components, utilities) use this pattern exclusively:

```cpp
// In any .cpp — one include, no context object required
#include "Backend/GameCoreBackend.h"

// Logging
FGameCoreBackend::GetLogging()->LogWarning(TEXT("Persistence"), Message);
FGameCoreBackend::GetLogging()->LogError(TEXT("Leveling"), FString::Printf(TEXT("XP overflow for entity %s"), *Id.ToString()));

// Key-value storage (named backend)
FGameCoreBackend::GetKeyStorage(TEXT("PlayerDB"))->Set(Tag, EntityId, Data, false, false);

// Default key-value storage
FGameCoreBackend::GetKeyStorage()->Set(Tag, EntityId, Data, true, true);

// Audit
FGameCoreBackend::GetAudit(TEXT("Security"))->RecordEvent(CheatEntry);
```

**Do not** call `GetSubsystem<UGameCoreBackendSubsystem>()` anywhere inside the GameCore plugin. Use `FGameCoreBackend` instead.

---

## Null Fallback Behavior

When `Instance` is null (subsystem not initialized, or already deinitialized), all getters return static null implementations. Each null implementation routes to `UE_LOG` so no calls are silently swallowed:

| Service | Null Fallback | UE_LOG Behavior |
|---|---|---|
| `ILoggingService` | `FNullLoggingService` | Maps `ELogSeverity` → `UE_LOG(LogGameCore, ...)` |
| `IKeyStorageService` | `FNullKeyStorageService` | `UE_LOG(LogGameCore, Warning, ...)` per call |
| `IQueryStorageService` | `FNullQueryStorageService` | `UE_LOG(LogGameCore, Warning, ...)` per call |
| `IAuditService` | `FNullAuditService` | `UE_LOG(LogGameCore, Warning, ...)` per call |

See individual service spec pages for the exact `UE_LOG` strings each null method emits.

---

## Thread Safety

`Register`/`Unregister` are called on the game thread during subsystem init/deinit. The `Instance` pointer read in each getter is not atomic.

**Safe cases (no action needed):**
- All calls from the game thread (the common case for all current GameCore systems)
- `ILoggingService::Log()` is internally thread-safe (lock-free queue) — calling `GetLogging()` on the game thread and passing the pointer to a background thread is safe for the duration of the subsystem's lifetime

**Unsafe case:**
- Background tasks that outlive `UGameCoreBackendSubsystem::Deinitialize()` and call `FGameCoreBackend::GetLogging()` after `Unregister()` has been called. This is a lifecycle bug — tasks must complete or be cancelled before `Deinitialize` returns.

If a future system requires background-safe access after teardown, make `Instance` a `std::atomic<UGameCoreBackendSubsystem*>`.

---

## Notes

- `FGameCoreBackend` has no UObject overhead — it is a plain struct with only static methods and one static pointer.
- The static null fallbacks (`GNullLogging`, etc.) are file-scope statics in `GameCoreBackend.cpp`. They are constructed at module load and never destroyed until module unload — no order-of-destruction issues.
- This struct must **not** be used in client-side code. All backend services are server-only. `UGameCoreBackendSubsystem::ShouldCreateSubsystem` enforces this at the subsystem level; `FGameCoreBackend::Instance` will be null on clients naturally.
