# GameCore Backend

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Backend/`
**Type:** `UGameInstanceSubsystem`

Central subsystem for all external backend service integrations. Manages the lifecycle of **named service instances** across four interface types: key-value storage, query storage, audit, and logging. Acts as the single injection point for backend transport implementations — GameCore itself has **no knowledge of concrete backends** (Redis, Postgres, Datadog, etc.).

Multiple named instances of any service type are supported. This allows different game systems to route to distinct backends — for example, a `"PlayerDB"` and an `"EconomyDB"` key-value store, or a `"Security"` and a `"Gameplay"` audit service.

All service types fall back to safe null implementations when not registered or not connected. Accessors never return null.

The subsystem is **server-only** by default via `ShouldCreateSubsystem`.

---

## Sub-pages

- IDBService
- IAuditService
- ILoggingService

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Backend/
            ├── BackendSubsystem.h/.cpp
            ├── DBService.h
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
    // Never returns null.
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

    // Null fallbacks — always valid, route to UE_LOG or no-op
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

## Deinitialize — Graceful Shutdown

`Deinitialize` calls `Flush()` on **all** registered buffered services (audit and logging) before tearing down. This guarantees no in-flight events are lost on shutdown regardless of how many named services are registered.

```cpp
void UGameCoreBackendSubsystem::Deinitialize()
{
    for (auto& [Key, Service] : AuditServices)
        if (ConnectedAudit.Contains(Key))
            Service.GetInterface()->Flush();

    for (auto& [Key, Service] : LoggingServices)
        if (ConnectedLogging.Contains(Key))
            Service.GetInterface()->Flush();

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

## Usage Example — Multi-Backend Wiring

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

// Callers select the right backend by name:
Backend->GetKeyStorage(TEXT("EconomyDB"))->Set(Key, Value);
Backend->GetAudit(TEXT("Security"))->RecordEvent(CheatEntry);
Backend->GetLogging()->LogWarning(TEXT("Persistence"), Message); // resolves NAME_None
```

---

## Notes

- Services must be registered **before** `Initialize` is called. The subsystem does not retry connections.
- All accessors (`GetAudit`, `GetLogging`, etc.) are always safe to call — they never return null.
- `NAME_None` is the conventional key for the single general-purpose instance of a service type. Use a consistent convention per service type across the project.
- `Flush()` is called on **all** registered audit and logging services during `Deinitialize` — not just `NAME_None`. Every named instance is flushed.
- `IKeyStorageService` and `IQueryStorageService` do not buffer — no `Flush()` call is needed for them on shutdown.
- `friend` declarations are intentional: `Connect()` is public on each interface for technical reasons but callers outside the subsystem must not call it directly.
- In PIE, `ShouldCreateSubsystem` may return false depending on net mode. Use a dedicated server PIE configuration for local backend testing.
