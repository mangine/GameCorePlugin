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

```cpp
// Any .cpp in the plugin — one include, no context object
#include "Backend/GameCoreBackend.h"

FGameCoreBackend::GetLogging()->LogWarning(TEXT("MySystem"), Message);
FGameCoreBackend::GetKeyStorage(TEXT("PlayerDB"))->Set(Tag, Id, Data, false, false);
FGameCoreBackend::GetAudit(TEXT("Security"))->RecordEvent(Entry);
```

See the **FGameCoreBackend** sub-page for full specification.

---

## Sub-pages

- [FGameCoreBackend](GameCore%20Backend/FGameCoreBackend.md) — static facade, usage patterns, null fallback behavior
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
            ├── GameCoreBackend.h       ← Static facade (new — HAP-6)
            ├── GameCoreBackend.cpp     ← Static facade impl + null fallback statics
            ├── BackendSubsystem.h/.cpp
            ├── KeyStorageService.h
            ├── QueryStorageService.h
            ├── AuditService.h
            └── LoggingService.h
```

---

## Named Service Model

Each service type is stored in a `TMap<FName, TScriptInterface<T>>`. Callers select which backend to use by passing an `FName` key. If the requested key is not registered or failed to connect, the null fallback is returned.

**Recommended key conventions:**

| Key | Typical Use |
|---|---|
| `NAME_None` | General-purpose default — used by callers that don't need routing |
| `"PlayerDB"` | Player character persistence |
| `"EconomyDB"` | Market, currency, trades |
| `"Security"` | Anti-cheat audit stream |
| `"Gameplay"` | Quest, progression audit stream |

Callers that don't need routing use `GetAudit()` (no argument), which resolves to `NAME_None` or the null fallback.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UGameCoreBackendSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

    // Only the subsystem may call Connect() on services
    friend class IKeyStorageService;
    friend class IQueryStorageService;
    friend class IAuditService;
    friend class ILoggingService;

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // --- Service Registration ---
    // Call before Initialize completes (e.g. from GameInstance::Init or an early subsystem).
    // Key identifies this named instance — use NAME_None for the primary/default service.
    void RegisterKeyStorageService  (FName Key, TScriptInterface<IKeyStorageService>   Service, const FString& ConnectionString);
    void RegisterQueryStorageService(FName Key, TScriptInterface<IQueryStorageService> Service, const FString& ConnectionString);
    void RegisterAuditService       (FName Key, TScriptInterface<IAuditService>        Service, const FString& ConnectionString);
    void RegisterLoggingService     (FName Key, TScriptInterface<ILoggingService>      Service, const FLoggingConfig& Config);

    // --- Service Accessors ---
    // Returns the named service, or the null fallback if not registered / not connected.
    // Never returns null. Null fallbacks route to UE_LOG.
    IKeyStorageService*   GetKeyStorage  (FName Key = NAME_None) const;
    IQueryStorageService* GetQueryStorage(FName Key = NAME_None) const;
    IAuditService*        GetAudit       (FName Key = NAME_None) const;
    ILoggingService*      GetLogging     (FName Key = NAME_None) const;

private:
    TMap<FName, TScriptInterface<IKeyStorageService>>   KeyStorageServices;
    TMap<FName, TScriptInterface<IQueryStorageService>> QueryStorageServices;
    TMap<FName, TScriptInterface<IAuditService>>        AuditServices;
    TMap<FName, TScriptInterface<ILoggingService>>      LoggingServices;

    TSet<FName> ConnectedKeyStorage;
    TSet<FName> ConnectedQueryStorage;
    TSet<FName> ConnectedAudit;
    TSet<FName> ConnectedLogging;

    // Null fallbacks — always valid, route all calls to UE_LOG.
    // Owned by the subsystem; also referenced as static fallbacks in GameCoreBackend.cpp.
    TUniquePtr<FNullKeyStorageService>   NullKeyStorage;
    TUniquePtr<FNullQueryStorageService> NullQueryStorage;
    TUniquePtr<FNullAuditService>        NullAudit;
    TUniquePtr<FNullLoggingService>      NullLogging;
};
```

---

## Accessors — Fallback Logic

Accessors look up the named key first. If not found or not connected, the null fallback is returned:

```cpp
IAuditService* UGameCoreBackendSubsystem::GetAudit(FName Key) const
{
    const auto* Entry = AuditServices.Find(Key);
    if (Entry && ConnectedAudit.Contains(Key))
        return Entry->GetInterface();
    return NullAudit.Get();
}
// Same pattern for GetKeyStorage(), GetQueryStorage(), GetLogging()
```

---

## Registration & Connection

```cpp
void UGameCoreBackendSubsystem::RegisterAuditService(
    FName Key, TScriptInterface<IAuditService> Service, const FString& ConnectionString)
{
    AuditServices.Add(Key, Service);
    if (Service.GetInterface()->Connect(ConnectionString))
        ConnectedAudit.Add(Key);
    else
        UE_LOG(LogGameCore, Error, TEXT("[Backend] AuditService '%s' failed to connect."), *Key.ToString());
}
// Same pattern for RegisterKeyStorageService(), RegisterQueryStorageService(), RegisterLoggingService()
```

Register services from your game's `UGameInstance::Init` **before** `Super::Init()` completes, or from a `UGameInstanceSubsystem` with an earlier initialization order.

---

## Initialize — FGameCoreBackend Registration

`Initialize` registers `this` with `FGameCoreBackend` so all plugin code can access services via the static facade immediately after the subsystem comes online.

```cpp
void UGameCoreBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Bring null fallbacks online before registering —
    // so any log call that happens during service registration is already safe.
    NullKeyStorage   = MakeUnique<FNullKeyStorageService>();
    NullQueryStorage = MakeUnique<FNullQueryStorageService>();
    NullAudit        = MakeUnique<FNullAuditService>();
    NullLogging      = MakeUnique<FNullLoggingService>();

    // Register the static facade — from this point, FGameCoreBackend::GetLogging() etc. are live.
    FGameCoreBackend::Register(this);
}
```

---

## Deinitialize — Ordered Shutdown

Shutdown order is critical. Flush first, unregister the facade second, clear maps last. Any logging that happens during map teardown still resolves safely to null fallbacks via the static statics in `GameCoreBackend.cpp`.

```cpp
void UGameCoreBackendSubsystem::Deinitialize()
{
    // 1. Flush all buffered services — no data loss on shutdown
    for (auto& [Key, Service] : LoggingServices)
        if (ConnectedLogging.Contains(Key))
            Service.GetInterface()->Flush();

    for (auto& [Key, Service] : AuditServices)
        if (ConnectedAudit.Contains(Key))
            Service.GetInterface()->Flush();

    // 2. Unregister facade — FGameCoreBackend::GetX() now routes to static null fallbacks in .cpp
    FGameCoreBackend::Unregister();

    // 3. Clear service maps
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

## Usage Example — Multi-Backend Wiring (Game Module)

```cpp
void UMyGameInstance::Init()
{
    auto* Backend = GetSubsystem<UGameCoreBackendSubsystem>();

    // Two separate key-value stores
    Backend->RegisterKeyStorageService(TEXT("PlayerDB"),  PlayerRedis,  TEXT("redis://player-host:6379"));
    Backend->RegisterKeyStorageService(TEXT("EconomyDB"), EconomyRedis, TEXT("redis://economy-host:6379"));

    // Two audit streams
    Backend->RegisterAuditService(TEXT("Security"), SecurityAudit, TEXT("postgres://audit-host/security"));
    Backend->RegisterAuditService(TEXT("Gameplay"), GameplayAudit, TEXT("postgres://audit-host/gameplay"));

    // One default logging service
    FLoggingConfig LogConfig;
    LogConfig.Endpoint              = TEXT("https://logs.datadoghq.com/...");
    LogConfig.FlushIntervalSeconds  = 5.0f;
    LogConfig.FlushThresholdPercent = 0.75f;
    LogConfig.MaxBatchSize          = 500;
    Backend->RegisterLoggingService(NAME_None, MyDatadogLogger, LogConfig);

    Super::Init();
}
```

After this, all GameCore plugin systems automatically route through `FGameCoreBackend` with no additional wiring required.

---

## Notes

- Services must be registered **before** `Initialize` completes. The subsystem does not retry connections.
- All `FGameCoreBackend::GetX()` calls are always safe — they never return null before, during, or after the subsystem's lifetime.
- `NAME_None` is the conventional key for the single general-purpose instance of a service type.
- `Flush()` is called on **all** registered audit and logging services during `Deinitialize` — not just `NAME_None`.
- `IKeyStorageService` and `IQueryStorageService` do not buffer — no `Flush()` call is needed for them on shutdown.
- `friend` declarations on the subsystem are intentional: `Connect()` is technically public on each interface but must only be called by the subsystem.
- In PIE, `ShouldCreateSubsystem` may return false depending on net mode. `FGameCoreBackend::Instance` will be null on clients — all calls transparently fall through to `UE_LOG` null fallbacks.
