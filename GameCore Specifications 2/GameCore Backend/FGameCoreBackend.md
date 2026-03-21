# FGameCoreBackend

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Backend/GameCoreBackend.h` / `GameCoreBackend.cpp`  
**Type:** Plain C++ struct (static facade, no UObject)  

---

## Responsibility

Zero-cost static facade over `UGameCoreBackendSubsystem`. Provides uniform access to all backend services from any layer (subsystems, components, utilities) with a single include and no context object. All GameCore plugin systems access backend services exclusively through this struct.

Also exposes lightweight `TFunction` delegate hooks for wiring to any custom system without implementing service interfaces or using the subsystem at all.

---

## Header

```cpp
// GameCoreBackend.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UGameCoreBackendSubsystem;
class ILoggingService;
class IKeyStorageService;
class IQueryStorageService;
class IAuditService;
struct FAuditEntry;
enum class ELogSeverity : uint8;

/**
 * Static facade over UGameCoreBackendSubsystem.
 *
 * Registered by UGameCoreBackendSubsystem::Initialize / Deinitialize. Never call Register/Unregister directly.
 *
 * Delegate hooks (OnLog, OnAudit, OnPersistenceWrite) take priority over the subsystem path when bound.
 * When unbound: one null-check branch, branch-predicted-not-taken — effectively zero overhead.
 *
 * Tag-based accessors resolve FGameplayTag -> FName via the subsystem routing map.
 * Unregistered tags fall back to NAME_None (the default service).
 *
 * Never returns null. Always safe to call including before subsystem init and during teardown.
 *
 * Server-side only. Never use from client code.
 */
struct GAMECORE_API FGameCoreBackend
{
    // --- Subsystem Registration (called by UGameCoreBackendSubsystem only) ---
    static void Register  (UGameCoreBackendSubsystem* Subsystem);
    static void Unregister();

    // -------------------------------------------------------------------------
    // Lightweight Delegate Hooks
    // Bind in GameInstance::Init. Clear in GameInstance::Shutdown.
    // When bound, the delegate fires INSTEAD of the subsystem path.
    // -------------------------------------------------------------------------
    static TFunction<void(ELogSeverity, const FString&, const FString&, const FString&)> OnLog;
    static TFunction<void(const FAuditEntry&)>                                           OnAudit;
    static TFunction<void(FGameplayTag, FGuid, TArrayView<const uint8>)>                 OnPersistenceWrite;

    // -------------------------------------------------------------------------
    // Canonical Call Methods
    // Preferred over GetX()->Method(). Routes delegate -> subsystem -> null fallback.
    // Logging always reaches UE_LOG at minimum — never silently dropped.
    // -------------------------------------------------------------------------
    static void Log(
        ELogSeverity   Severity,
        const FString& Category,
        const FString& Message,
        const FString& Payload = FString{});

    static void Audit(const FAuditEntry& Entry);

    static void PersistenceWrite(
        FGameplayTag            Tag,
        FGuid                   EntityId,
        TArrayView<const uint8> Bytes);

    // -------------------------------------------------------------------------
    // Tag-Based Service Accessors
    // Use when you need lower-level interface methods (Query, GetById, RecordBatch, etc.)
    // Resolves FGameplayTag -> FName via subsystem routing map.
    // -------------------------------------------------------------------------
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
    // Raw pointer — lifetime controlled by UGameCoreBackendSubsystem.
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

// Static members
UGameCoreBackendSubsystem* FGameCoreBackend::Instance = nullptr;

TFunction<void(ELogSeverity, const FString&, const FString&, const FString&)>
    FGameCoreBackend::OnLog = nullptr;
TFunction<void(const FAuditEntry&)>
    FGameCoreBackend::OnAudit = nullptr;
TFunction<void(FGameplayTag, FGuid, TArrayView<const uint8>)>
    FGameCoreBackend::OnPersistenceWrite = nullptr;

// File-scope null statics — constructed at module load, never destroyed until module unload.
// Safe to return pointers to; no order-of-destruction issues.
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
    GetLogging(FGameplayTag{})->Log(Severity, Category, Message, Payload);
}

void FGameCoreBackend::Audit(const FAuditEntry& Entry)
{
    if (OnAudit)
    {
        OnAudit(Entry);
        return;
    }
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
    return &GNullAudit;
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

## Null Fallback Behaviour

| Service | No Subsystem + No Delegate | Behaviour |
|---|---|---|
| `ILoggingService` | `GNullLogging` | Routes every call to `UE_LOG(LogGameCore, ...)` — never silent |
| `IAuditService` | `GNullAudit` | Silent no-op — audit is opt-in infrastructure |
| `IKeyStorageService` | `GNullKeyStorage` | Writes: `UE_LOG Warning`. Reads: `bSuccess=false` callback |
| `IQueryStorageService` | `GNullQueryStorage` | Writes: `UE_LOG Warning`. Reads: `bSuccess=false` callback |

---

## Thread Safety

- `Register`/`Unregister` are called on the game thread only.
- `Instance` pointer read is not atomic — safe for all game-thread callers.
- `ILoggingService::Log()` is internally thread-safe (lock-free queue). Calling `GetLogging()` on the game thread and using the returned pointer from a background thread is safe for the duration of the subsystem's lifetime.
- Background tasks must complete or be cancelled before `Deinitialize()` returns. A task that calls `FGameCoreBackend::GetX()` after `Unregister()` will receive the file-scope null static — safe, but no subsystem data.
- If future systems require background-safe access after teardown, make `Instance` a `std::atomic<UGameCoreBackendSubsystem*>`.

---

## Notes

- `FGameCoreBackend` has no UObject overhead — plain struct, static methods, one static pointer.
- File-scope null statics (`GNullLogging`, etc.) are constructed at module load and survive until module unload — no order-of-destruction risk.
- Tag routing resolution is an O(1) `TMap` lookup — negligible cost.
- **Server-side only.** Must not be called from client code.
