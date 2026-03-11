# GameCore Backend

**Module:** `GameCore`

**Location:** `GameCore/Source/GameCore/Backend/`

**Type:** `UGameInstanceSubsystem`

Central subsystem for all external backend service integrations. Owns and manages the lifecycle of four service interfaces: key storage, query storage, audit, and logging. Acts as the single injection point for backend transport implementations — GameCore itself has **no knowledge of concrete backends** (Redis, Postgres, Datadog, etc.).

The subsystem enforces that all service connections are established during `Initialize` and torn down during `Deinitialize`. It is **server-only** by default via `ShouldCreateSubsystem`.

All services fall back to `UE_LOG` routing automatically when not registered or not yet connected. Systems that depend on these interfaces never need to null-check — they always receive a valid, safe implementation.

---

## Service Responsibilities

| Service | Interface | Backend Examples | Use For |
|---|---|---|---|
| Key Storage | `IKeyStorageService` | Redis, MongoDB, DynamoDB | Entity blobs, session state, ephemeral data with TTL |
| Query Storage | `IQueryStorageService` | PostgreSQL, Elasticsearch, CockroachDB | Market listings, leaderboards, searchable structured records |
| Audit | `IAuditService` | Kafka, append-only event store | Gameplay event audit trail, anti-cheat, rollback |
| Logging | `ILoggingService` | Datadog, Loki, CloudWatch | Server log forwarding |

---

## Sub-pages

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
            ├── BackendSubsystem.h/.cpp
            ├── KeyStorageService.h
            ├── QueryStorageService.h
            ├── AuditService.h
            └── LoggingService.h
```

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
    // Call before Initialize completes (e.g. from GameInstance::Init)
    void RegisterKeyStorageService  (TScriptInterface<IKeyStorageService>   Service, const FString& ConnectionString);
    void RegisterQueryStorageService(TScriptInterface<IQueryStorageService> Service, const FString& ConnectionString);
    void RegisterAuditService       (TScriptInterface<IAuditService>        Service, const FString& ConnectionString);
    void RegisterLoggingService     (TScriptInterface<ILoggingService>      Service, const FString& ConnectionString);

    // --- Service Accessors ---
    // Always safe — never return null. Return null fallback if not connected.
    IKeyStorageService*   GetKeyStorage()   const;
    IQueryStorageService* GetQueryStorage() const;
    IAuditService*        GetAudit()        const;
    ILoggingService*      GetLogging()      const;

private:
    TScriptInterface<IKeyStorageService>   KeyStorageService;
    TScriptInterface<IQueryStorageService> QueryStorageService;
    TScriptInterface<IAuditService>        AuditService;
    TScriptInterface<ILoggingService>      LoggingService;

    // Null fallbacks — always valid, route to UE_LOG
    TUniquePtr<FNullKeyStorageService>   NullKeyStorage;
    TUniquePtr<FNullQueryStorageService> NullQueryStorage;
    TUniquePtr<FNullAuditService>        NullAudit;
    TUniquePtr<FNullLoggingService>      NullLogging;

    bool bKeyStorageConnected   = false;
    bool bQueryStorageConnected = false;
    bool bAuditConnected        = false;
    bool bLoggingConnected      = false;
};
```

---

## Accessors — Fallback Logic

Accessors never return null. If a service is not registered or failed to connect, they return the null fallback which routes to `UE_LOG`:

```cpp
IKeyStorageService* UGameCoreBackendSubsystem::GetKeyStorage() const
{
    if (KeyStorageService.GetObject() && bKeyStorageConnected)
        return KeyStorageService.GetInterface();
    return NullKeyStorage.Get();
}

IQueryStorageService* UGameCoreBackendSubsystem::GetQueryStorage() const
{
    if (QueryStorageService.GetObject() && bQueryStorageConnected)
        return QueryStorageService.GetInterface();
    return NullQueryStorage.Get();
}
// Same pattern for GetAudit() and GetLogging()
```

---

## Registration & Connection

```cpp
void UGameCoreBackendSubsystem::RegisterKeyStorageService(
    TScriptInterface<IKeyStorageService> Service, const FString& ConnectionString)
{
    KeyStorageService = Service;
    bKeyStorageConnected = Service.GetInterface()->Connect(ConnectionString);
    if (!bKeyStorageConnected)
        UE_LOG(LogGameCore, Error, TEXT("[Backend] KeyStorageService failed to connect."));
}

void UGameCoreBackendSubsystem::RegisterQueryStorageService(
    TScriptInterface<IQueryStorageService> Service, const FString& ConnectionString)
{
    QueryStorageService = Service;
    bQueryStorageConnected = Service.GetInterface()->Connect(ConnectionString);
    if (!bQueryStorageConnected)
        UE_LOG(LogGameCore, Error, TEXT("[Backend] QueryStorageService failed to connect."));
}
// Same pattern for RegisterAuditService and RegisterLoggingService
```

Register services from your game's `UGameInstance::Init` **before** `Super::Init()` completes, or from a `UGameInstanceSubsystem` with an earlier initialization order.

---

## Deinitialize — Graceful Shutdown

`Deinitialize` calls `Flush()` on services that buffer data before tearing them down. This guarantees in-flight audit events and log messages are drained before the process exits.

```cpp
void UGameCoreBackendSubsystem::Deinitialize()
{
    // Flush buffered services before teardown
    if (AuditService.GetObject() && bAuditConnected)
        AuditService.GetInterface()->Flush();

    if (LoggingService.GetObject() && bLoggingConnected)
        LoggingService.GetInterface()->Flush();

    // Reset all services
    KeyStorageService   = nullptr;
    QueryStorageService = nullptr;
    AuditService        = nullptr;
    LoggingService      = nullptr;

    bKeyStorageConnected   = false;
    bQueryStorageConnected = false;
    bAuditConnected        = false;
    bLoggingConnected      = false;
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

## Notes

- Services must be registered **before** `Initialize` calls `Connect`. The subsystem does not retry connections.
- All accessors are always safe to call — they never return null.
- `KeyStorageService` and `QueryStorageService` may point to different backend hosts, different databases, or even the same host with different routing logic. The game module decides.
- `IAuditService` requires `SetServerId()` to be called after registration before events are dispatched. Until then, events queue internally. See `IAuditService` for queue behavior and cap details.
- The `friend` declarations are intentional: `Connect()` is public on each interface for technical reasons but callers outside the subsystem must not call it directly.
- In PIE, `ShouldCreateSubsystem` may return false depending on net mode. Use a dedicated server PIE configuration for local backend testing.
