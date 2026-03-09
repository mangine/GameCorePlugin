# GameCore Backend

**Module:** `GameCore`

**Location:** `GameCore/Source/GameCore/Backend/`

**Type:** `UGameInstanceSubsystem`

Central subsystem for all external backend service integrations. Owns and manages the lifecycle of three service interfaces: database, audit, and logging. Acts as the single injection point for backend transport implementations — GameCore itself has **no knowledge of concrete backends** (Redis, Postgres, Datadog, etc.).

The subsystem enforces that all service connections are established during `Initialize` and torn down during `Deinitialize`. It is **server-only** by default via `ShouldCreateSubsystem`.

All three services fall back to `UE_LOG` routing automatically when not registered or not yet connected. This means systems that depend on these interfaces never need to null-check — they always get a valid, safe implementation.

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

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UGameCoreBackendSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

    // Only the subsystem may call Connect() on services
    friend class IDBService;
    friend class IAuditService;
    friend class ILoggingService;

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // --- Service Registration ---
    // Call before Initialize completes (e.g. from GameInstance::Init)
    void RegisterDBService(TScriptInterface<IDBService> Service, const FString& ConnectionString);
    void RegisterAuditService(TScriptInterface<IAuditService> Service, const FString& ConnectionString);
    void RegisterLoggingService(TScriptInterface<ILoggingService> Service, const FString& ConnectionString);

    // --- Service Accessors ---
    IDBService*      GetDB()      const;
    IAuditService*   GetAudit()   const;
    ILoggingService* GetLogging() const;

private:
    TScriptInterface<IDBService>      DBService;
    TScriptInterface<IAuditService>   AuditService;
    TScriptInterface<ILoggingService> LoggingService;

    // Null fallbacks — always valid, route to UE_LOG
    TUniquePtr<FNullDBService>      NullDB;
    TUniquePtr<FNullAuditService>   NullAudit;
    TUniquePtr<FNullLoggingService> NullLogging;

    bool bDBConnected      = false;
    bool bAuditConnected   = false;
    bool bLoggingConnected = false;
};
```

---

## Accessors — Fallback Logic

Accessors never return null. If a service is not registered or failed to connect, they return the null fallback which routes to `UE_LOG`:

```cpp
IDBService* UGameCoreBackendSubsystem::GetDB() const
{
    if (DBService.GetObject() && bDBConnected)
        return DBService.GetInterface();
    return NullDB.Get();
}
// Same pattern for GetAudit() and GetLogging()
```

---

## Registration & Connection

```cpp
void UGameCoreBackendSubsystem::RegisterDBService(
    TScriptInterface<IDBService> Service, const FString& ConnectionString)
{
    DBService = Service;
    bDBConnected = Service.GetInterface()->Connect(ConnectionString);
    if (!bDBConnected)
        UE_LOG(LogGameCore, Error, TEXT("[Backend] DBService failed to connect."));
}
```

Register services from your game's `UGameInstance::Init` **before** `Super::Init()` completes, or from a `UGameInstanceSubsystem` with an earlier initialization order.

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
- `GetDB()`, `GetAudit()`, `GetLogging()` are always safe to call — they never return null.
- The `friend` declarations are intentional: `Connect()` is public on the interface for technical reasons but callers outside the subsystem should not call it directly. Document this contract clearly for implementors.
- In PIE, `ShouldCreateSubsystem` may return false depending on net mode. Use a dedicated server PIE configuration for local backend testing.

[IDBService](GameCore%20Backend/IDBService%2031cd261a36cf8198ad66ec7abcc8fe97.md)

[IAuditService](GameCore%20Backend/IAuditService%2031cd261a36cf81a6a338eaecf802df1f.md)

[ILoggingService](GameCore%20Backend/ILoggingService%2031cd261a36cf81de96decd78a3ca7888.md)