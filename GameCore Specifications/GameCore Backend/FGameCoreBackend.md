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

With tag-based routing, systems also no longer need to know the `FName` key of the backend instance they should write to — they pass their own `FGameplayTag` and the routing map resolves the correct instance automatically.

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
 * Falls back to null implementations (UE_LOG routing) when the subsystem is not live.
 *
 * Tag-based overloads resolve FGameplayTag → FName via the subsystem's routing maps.
 * Systems pass their own tag and never need to know which backend instance to use.
 *
 * FName overloads remain for game-module-level code that needs explicit routing.
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

    // --- Tag-Based Accessors (preferred for plugin systems) ---
    // Resolves the FGameplayTag to an FName via the subsystem's routing map.
    // Falls back to NAME_None (the default service) if no mapping is registered for the tag.
    // Returns the null fallback if the subsystem is not live.
    static IKeyStorageService*   GetKeyStorage  (FGameplayTag Tag);
    static IQueryStorageService* GetQueryStorage(FGameplayTag Tag);
    static IAuditService*        GetAudit       (FGameplayTag Tag);

    // --- FName-Based Accessors (for game module wiring code) ---
    // Select a specific named backend instance directly.
    // Use when the caller explicitly knows which instance it needs (e.g. GameInstance::Init wiring).
    // Falls back to the null fallback if the key is not registered or subsystem is not live.
    static IKeyStorageService*   GetKeyStorage  (FName Key = NAME_None);
    static IQueryStorageService* GetQueryStorage(FName Key = NAME_None);
    static IAuditService*        GetAudit       (FName Key = NAME_None);

    // --- Logging (always FName-based — one logger for the whole server) ---
    static ILoggingService*      GetLogging     (FName Key = NAME_None);

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

// Static instance — set by UGameCoreBackendSubsystem::Initialize
UGameCoreBackendSubsystem* FGameCoreBackend::Instance = nullptr;

// Static null fallbacks — constructed once at module load, never destroyed until module unload.
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

// --- Tag-based overloads ---
// Tag → FName resolution is delegated to the subsystem's routing maps.
// If no mapping exists for the tag, NAME_None is used, which resolves to the default service.

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
    return &GNullAudit;
}

// --- FName-based overloads ---

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
}
```

---

## Tag Routing — How It Works

The game module registers tag→name mappings on the subsystem once during `GameInstance::Init`. After that, any plugin system can call `FGameCoreBackend::GetKeyStorage(MyTag)` without knowing which backend instance it maps to.

```
FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)
    → Instance->ResolveKeyStorageTag(TAG_Persistence_Entity_Player)
    → KeyStorageRoutes.Find(tag) → "PlayerDB"
    → Instance->GetKeyStorage("PlayerDB")
    → returns the registered Redis instance for player data
```

If no mapping exists for the tag, `ResolveKeyStorageTag` returns `NAME_None`, which resolves to the default registered service (or the null fallback if none is registered).

**Resolution is exact-match only.** `TAG_Persistence_Entity_Player` and `TAG_Persistence_Entity_Player_Inventory` are independent keys — if you want both to route to `"PlayerDB"`, register both explicitly.

---

## Usage — Call Site Pattern

```cpp
// In any plugin .cpp — one include, no context object required
#include "Backend/GameCoreBackend.h"

// Tag-based (preferred inside plugin systems — no backend name knowledge required)
FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)->Set(TAG_Persistence_Entity_Player, EntityId, Data, false, false);
FGameCoreBackend::GetAudit(TAG_Audit_Progression)->RecordEvent(Entry);
FGameCoreBackend::GetQueryStorage(TAG_Schema_Market_Listing)->Query(TAG_Schema_Market_Listing, Filter, Callback);

// Logging always FName-based — one logger for all systems
FGameCoreBackend::GetLogging()->LogWarning(TEXT("Persistence"), Message);
FGameCoreBackend::GetLogging()->LogError(TEXT("Leveling"), FString::Printf(TEXT("XP overflow for %s"), *Id.ToString()));

// FName-based (game module wiring code only)
FGameCoreBackend::GetKeyStorage(TEXT("PlayerDB"))->Set(...);
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
| `IAuditService` | `FNullAuditService` | `UE_LOG(LogGameCore, Log, ...)` per event |

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
- Tag routing resolution (`ResolveKeyStorageTag` etc.) is an O(1) `TMap` lookup — negligible cost.
- This struct must **not** be used in client-side code. All backend services are server-only.
