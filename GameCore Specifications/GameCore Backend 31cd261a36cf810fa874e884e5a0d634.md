# GameCore Backend

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Backend/`
**Type:** `UGameInstanceSubsystem` + static facade

Central subsystem for all external backend service integrations. Manages the lifecycle of **named service instances** across four interface types: key-value storage, query storage, audit, and logging. Acts as the single injection point for backend transport implementations — GameCore itself has **no knowledge of concrete backends** (Redis, Postgres, Datadog, etc.).

Multiple named instances of any service type are supported. This allows different game systems to route to distinct backends — for example, a `"PlayerDB"` and an `"EconomyDB"` key-value store, or a `"Security"` and a `"Gameplay"` audit service.

All service types fall back to safe null implementations when not registered or not connected. Null implementations route all calls to `UE_LOG` so nothing is silently swallowed. Accessors never return null.

The subsystem is **server-only** by default via `ShouldCreateSubsystem`.

---

## Access Pattern — FGameCoreBackend

**All GameCore plugin code accesses backend services exclusively via `FGameCoreBackend`**, the static facade that sits in front of this subsystem. Direct `GetSubsystem<UGameCoreBackendSubsystem>()` calls are not permitted inside the plugin.

All four service types use the same **tag-based accessors** — plugin systems pass their own `FGameplayTag` and routing is resolved automatically. Game module wiring code uses **FName-based accessors** to register and explicitly target specific instances.

```cpp
// Plugin system (tag-based — no backend name knowledge required)
#include "Backend/GameCoreBackend.h"
FGameCoreBackend::GetLogging(TAG_Log_Persistence)->LogWarning(TEXT("Persistence"), Message);
FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)->Set(...);
FGameCoreBackend::GetAudit(TAG_Audit_Progression)->RecordEvent(...);
```

See the **FGameCoreBackend** sub-page for full specification.

---

## Sub-pages

- [FGameCoreBackend](GameCore%20Backend/FGameCoreBackend.md) — static facade, tag routing, usage patterns, null fallback behavior
- IKeyStorageService
- IQueryStorageService
- IAuditService
- ILoggingService

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Backend/
            ├── GameCoreBackend.h       ← Static facade
            ├── GameCoreBackend.cpp     ← Static facade impl + null fallback statics
            ├── BackendSubsystem.h/.cpp
            ├── KeyStorageService.h
            ├── QueryStorageService.h
            ├── AuditService.h
            └── LoggingService.h
```

---

## Named Service Model

Services are registered under `FName` keys. `FName` remains the internal identifier for backend instances. `FGameplayTag` is the **routing key** that systems use — the subsystem resolves tag → name transparently. This applies uniformly to all four service types.

**Recommended FName key conventions:**

| Key | Typical Use |
|---|---|
| `NAME_None` | General-purpose default |
| `"PlayerDB"` | Player character persistence |
| `"EconomyDB"` | Market, currency, trades |
| `"Security"` | Anti-cheat audit stream |
| `"Gameplay"` | Quest, progression audit stream |
| `"SecurityLog"` | Dedicated security log sink (optional) |

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UGameCoreBackendSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

    friend class IKeyStorageService;
    friend class IQueryStorageService;
    friend class IAuditService;
    friend class ILoggingService;

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // --- Service Registration ---
    // Register a named backend instance. Call from GameInstance::Init before Super::Init().
    void RegisterKeyStorageService  (FName Key, TScriptInterface<IKeyStorageService>   Service, const FKeyStorageConfig& Config);
    void RegisterQueryStorageService(FName Key, TScriptInterface<IQueryStorageService> Service, const FString& ConnectionString);
    void RegisterAuditService       (FName Key, TScriptInterface<IAuditService>        Service, const FString& ConnectionString);
    void RegisterLoggingService     (FName Key, TScriptInterface<ILoggingService>      Service, const FLoggingConfig& Config);

    // --- Tag Routing Registration ---
    // Map a FGameplayTag to a named backend instance.
    // After this, FGameCoreBackend::GetX(Tag) resolves to the named instance.
    // Call from GameInstance::Init alongside service registration.
    // Unmapped tags resolve to NAME_None (the default service).
    void MapTagToLogging     (FGameplayTag Tag, FName Key);
    void MapTagToKeyStorage  (FGameplayTag Tag, FName Key);
    void MapTagToQueryStorage(FGameplayTag Tag, FName Key);
    void MapTagToAudit       (FGameplayTag Tag, FName Key);

    // --- Tag Resolution (used internally by FGameCoreBackend) ---
    FName ResolveLoggingTag     (FGameplayTag Tag) const;
    FName ResolveKeyStorageTag  (FGameplayTag Tag) const;
    FName ResolveQueryStorageTag(FGameplayTag Tag) const;
    FName ResolveAuditTag       (FGameplayTag Tag) const;

    // --- FName-Based Service Accessors ---
    // Returns the named service, or the null fallback if not registered / not connected.
    // Never returns null.
    IKeyStorageService*   GetKeyStorage  (FName Key = NAME_None) const;
    IQueryStorageService* GetQueryStorage(FName Key = NAME_None) const;
    IAuditService*        GetAudit       (FName Key = NAME_None) const;
    ILoggingService*      GetLogging     (FName Key = NAME_None) const;

private:
    // --- Service Maps ---
    TMap<FName, TScriptInterface<IKeyStorageService>>   KeyStorageServices;
    TMap<FName, TScriptInterface<IQueryStorageService>> QueryStorageServices;
    TMap<FName, TScriptInterface<IAuditService>>        AuditServices;
    TMap<FName, TScriptInterface<ILoggingService>>      LoggingServices;

    TSet<FName> ConnectedKeyStorage;
    TSet<FName> ConnectedQueryStorage;
    TSet<FName> ConnectedAudit;
    TSet<FName> ConnectedLogging;

    // --- Tag Routing Maps ---
    // Game module populates these via MapTagTo*(). Plugin systems never read these directly.
    TMap<FGameplayTag, FName> KeyStorageRoutes;
    TMap<FGameplayTag, FName> QueryStorageRoutes;
    TMap<FGameplayTag, FName> AuditRoutes;
    TMap<FGameplayTag, FName> LoggingRoutes;

    // --- Null Fallbacks ---
    TUniquePtr<FNullKeyStorageService>   NullKeyStorage;
    TUniquePtr<FNullQueryStorageService> NullQueryStorage;
    TUniquePtr<FNullAuditService>        NullAudit;
    TUniquePtr<FNullLoggingService>      NullLogging;
};
```

---

## Tag Resolution Implementation

```cpp
FName UGameCoreBackendSubsystem::ResolveLoggingTag(FGameplayTag Tag) const
{
    const FName* Found = LoggingRoutes.Find(Tag);
    return Found ? *Found : NAME_None;
}
// Same pattern for ResolveKeyStorageTag(), ResolveQueryStorageTag(), ResolveAuditTag()
```

Resolution is **exact-match only** — O(1) `TMap` lookup. Register each tag that needs non-default routing explicitly.

---

## Tag Routing Registration

```cpp
void UGameCoreBackendSubsystem::MapTagToLogging(FGameplayTag Tag, FName Key)
{
    LoggingRoutes.Add(Tag, Key);
}
// Same pattern for MapTagToKeyStorage(), MapTagToQueryStorage(), MapTagToAudit()
```

---

## Accessors — Fallback Logic

```cpp
ILoggingService* UGameCoreBackendSubsystem::GetLogging(FName Key) const
{
    const auto* Entry = LoggingServices.Find(Key);
    if (Entry && ConnectedLogging.Contains(Key))
        return Entry->GetInterface();
    return NullLogging.Get();
}
// Same pattern for GetKeyStorage(), GetQueryStorage(), GetAudit()
```

---

## Initialize — FGameCoreBackend Registration

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
```

---

## Deinitialize — Ordered Shutdown

```cpp
void UGameCoreBackendSubsystem::Deinitialize()
{
    // 1. Flush all buffered services
    for (auto& [Key, Service] : LoggingServices)
        if (ConnectedLogging.Contains(Key))
            Service.GetInterface()->Flush();

    for (auto& [Key, Service] : AuditServices)
        if (ConnectedAudit.Contains(Key))
            Service.GetInterface()->Flush();

    // 2. Unregister facade — GetX() now routes to static null fallbacks
    FGameCoreBackend::Unregister();

    // 3. Clear all maps
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

## ShouldCreateSubsystem — Server Only

```cpp
bool UGameCoreBackendSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    UGameInstance* GI = Cast<UGameInstance>(Outer);
    if (!GI) return false;
    UWorld* World = GI->GetWorld();
    if (!World) return false;
    const ENetMode NetMode = World->GetNetMode();
    return NetMode == NM_DedicatedServer || NetMode == NM_ListenServer;
}
```

---

## Usage Example — Full Game Module Wiring

The game module registers services **and** tag routes in one place. After this, plugin systems need no knowledge of backend topology.

```cpp
void UMyGameInstance::Init()
{
    auto* Backend = GetSubsystem<UGameCoreBackendSubsystem>();

    // 1. Register backend instances
    Backend->RegisterKeyStorageService(TEXT("PlayerDB"),   PlayerRedis,     PlayerDbConfig);
    Backend->RegisterKeyStorageService(TEXT("EconomyDB"),  EconomyRedis,    EconomyDbConfig);
    Backend->RegisterQueryStorageService(TEXT("EconomyDB"), EconomyPostgres, TEXT("postgres://..."));
    Backend->RegisterAuditService(TEXT("Security"),  SecurityAudit,  TEXT("postgres://audit-host/security"));
    Backend->RegisterAuditService(TEXT("Gameplay"),  GameplayAudit,  TEXT("postgres://audit-host/gameplay"));
    Backend->RegisterLoggingService(NAME_None,        MyDatadogLogger, LogConfig);         // default logger
    Backend->RegisterLoggingService(TEXT("SecurityLog"), SecurityLogger, SecurityLogConfig); // optional dedicated sink

    // 2. Register tag routes — all four service types
    // Key storage
    Backend->MapTagToKeyStorage(TAG_Persistence_Entity_Player,   TEXT("PlayerDB"));
    Backend->MapTagToKeyStorage(TAG_Persistence_Entity_Quest,    TEXT("PlayerDB"));
    Backend->MapTagToKeyStorage(TAG_Persistence_Economy_Listing, TEXT("EconomyDB"));

    // Query storage
    Backend->MapTagToQueryStorage(TAG_Schema_Market_Listing, TEXT("EconomyDB"));
    Backend->MapTagToQueryStorage(TAG_Schema_Leaderboard,    TEXT("EconomyDB"));

    // Audit
    Backend->MapTagToAudit(TAG_Audit_Progression, TEXT("Gameplay"));
    Backend->MapTagToAudit(TAG_Audit_Market,      TEXT("Gameplay"));
    Backend->MapTagToAudit(TAG_Audit_AntiCheat,   TEXT("Security"));

    // Logging (optional — unmapped tags fall back to NAME_None default logger)
    Backend->MapTagToLogging(TAG_Log_Security, TEXT("SecurityLog"));

    Super::Init();
}
```

Plugin systems need no knowledge of this topology:

```cpp
// Routes to "SecurityLog" automatically
FGameCoreBackend::GetLogging(TAG_Log_Security)->LogError(TEXT("AntiCheat"), Details);

// Falls back to NAME_None default logger (no explicit mapping needed)
FGameCoreBackend::GetLogging(TAG_Log_Persistence)->LogWarning(TEXT("Persistence"), Message);

// Storage and audit routing unchanged
FGameCoreBackend::GetKeyStorage(Payload.PersistenceTag)->Set(
    Payload.PersistenceTag, Payload.EntityId, Bytes,
    Payload.bFlushImmediately, Payload.bCritical);
```

---

## Notes

- Services must be registered **and** tag routes mapped before `Initialize` completes.
- Unmapped tags resolve to `NAME_None` — ensure a default service is registered for `NAME_None`, or all unmapped calls will hit the null fallback.
- Tag routing is **exact-match only**. Each tag that needs non-default routing must be registered explicitly.
- `Flush()` is called on **all** registered audit and logging services during `Deinitialize`.
- `friend` declarations are intentional: `Connect()` is public on interfaces for technical reasons but must only be called by the subsystem.
