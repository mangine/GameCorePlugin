# UGameCoreBackendSubsystem

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Backend/BackendSubsystem.h` / `BackendSubsystem.cpp`  
**Type:** `UGameInstanceSubsystem`  

---

## Responsibility

Lifecycle manager for all backend service instances. Owns the registration maps, connected-state sets, and tag routing tables. Calls `Connect()` on each service during `Initialize`. Calls `Flush()` on logging and audit services during `Deinitialize`. Registers and unregisters the `FGameCoreBackend` facade.

Direct access by plugin systems is **not permitted**. Plugin systems use `FGameCoreBackend` exclusively.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UGameCoreBackendSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

    // Services call Connect() which is public on interfaces for technical reasons,
    // but must only be called by this subsystem.
    friend class IKeyStorageService;
    friend class IQueryStorageService;
    friend class IAuditService;
    friend class ILoggingService;

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // -------------------------------------------------------------------------
    // Service Registration
    // Call from GameInstance::Init, before Super::Init().
    // -------------------------------------------------------------------------
    void RegisterKeyStorageService  (FName Key, TScriptInterface<IKeyStorageService>   Service, const FKeyStorageConfig& Config);
    void RegisterQueryStorageService(FName Key, TScriptInterface<IQueryStorageService> Service, const FString& ConnectionString);
    void RegisterAuditService       (FName Key, TScriptInterface<IAuditService>        Service, const FString& ConnectionString);
    void RegisterLoggingService     (FName Key, TScriptInterface<ILoggingService>      Service, const FLoggingConfig& Config);

    // -------------------------------------------------------------------------
    // Tag Routing Registration
    // Map FGameplayTag -> FName. Unmapped tags resolve to NAME_None.
    // Call from GameInstance::Init alongside service registration.
    // -------------------------------------------------------------------------
    void MapTagToKeyStorage  (FGameplayTag Tag, FName Key);
    void MapTagToQueryStorage(FGameplayTag Tag, FName Key);
    void MapTagToAudit       (FGameplayTag Tag, FName Key);
    void MapTagToLogging     (FGameplayTag Tag, FName Key);

    // -------------------------------------------------------------------------
    // Tag Resolution — used internally by FGameCoreBackend only
    // -------------------------------------------------------------------------
    FName ResolveKeyStorageTag  (FGameplayTag Tag) const;
    FName ResolveQueryStorageTag(FGameplayTag Tag) const;
    FName ResolveAuditTag       (FGameplayTag Tag) const;
    FName ResolveLoggingTag     (FGameplayTag Tag) const;

    // -------------------------------------------------------------------------
    // FName-Based Accessors — used internally by FGameCoreBackend only
    // Never returns nullptr.
    // -------------------------------------------------------------------------
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

    // Tracks which services successfully connected — only connected services are served.
    TSet<FName> ConnectedKeyStorage;
    TSet<FName> ConnectedQueryStorage;
    TSet<FName> ConnectedAudit;
    TSet<FName> ConnectedLogging;

    // --- Tag Routing Maps ---
    TMap<FGameplayTag, FName> KeyStorageRoutes;
    TMap<FGameplayTag, FName> QueryStorageRoutes;
    TMap<FGameplayTag, FName> AuditRoutes;
    TMap<FGameplayTag, FName> LoggingRoutes;

    // --- Null Fallbacks ---
    // Heap-allocated so they outlive any accessor call during teardown.
    TUniquePtr<FNullKeyStorageService>   NullKeyStorage;
    TUniquePtr<FNullQueryStorageService> NullQueryStorage;
    TUniquePtr<FNullAuditService>        NullAudit;
    TUniquePtr<FNullLoggingService>      NullLogging;
};
```

---

## ShouldCreateSubsystem — Server Only

```cpp
bool UGameCoreBackendSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GI = Cast<UGameInstance>(Outer);
    if (!GI) return false;
    const UWorld* World = GI->GetWorld();
    if (!World) return false;
    const ENetMode NetMode = World->GetNetMode();
    return NetMode == NM_DedicatedServer || NetMode == NM_ListenServer;
}
```

---

## Initialize

```cpp
void UGameCoreBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Construct null fallbacks — always available, no connection required.
    NullKeyStorage   = MakeUnique<FNullKeyStorageService>();
    NullQueryStorage = MakeUnique<FNullQueryStorageService>();
    NullAudit        = MakeUnique<FNullAuditService>();
    NullLogging      = MakeUnique<FNullLoggingService>();

    // Connect all registered services.
    for (auto& [Key, Service] : KeyStorageServices)
    {
        // Config was stored during RegisterKeyStorageService.
        // Connection is attempted here, after registration.
        // If Connect() returns false, the key is not added to ConnectedKeyStorage
        // and the null fallback is used for all calls to that key.
        if (Service.GetInterface()->Connect(StoredKeyStorageConfigs.FindRef(Key)))
            ConnectedKeyStorage.Add(Key);
        else
            UE_LOG(LogGameCore, Error, TEXT("[Backend] KeyStorage '%s' failed to connect."), *Key.ToString());
    }

    for (auto& [Key, Service] : QueryStorageServices)
    {
        if (Service.GetInterface()->Connect(StoredQueryStorageConnectionStrings.FindRef(Key)))
            ConnectedQueryStorage.Add(Key);
        else
            UE_LOG(LogGameCore, Error, TEXT("[Backend] QueryStorage '%s' failed to connect."), *Key.ToString());
    }

    for (auto& [Key, Service] : AuditServices)
    {
        if (Service.GetInterface()->Connect(StoredAuditConnectionStrings.FindRef(Key)))
            ConnectedAudit.Add(Key);
        else
            UE_LOG(LogGameCore, Error, TEXT("[Backend] Audit '%s' failed to connect."), *Key.ToString());
    }

    for (auto& [Key, Service] : LoggingServices)
    {
        Service.GetInterface()->Initialize(StoredLoggingConfigs.FindRef(Key));
        ConnectedLogging.Add(Key); // Logging uses Initialize, not Connect — always considered connected.
    }

    FGameCoreBackend::Register(this);
}
```

> **Note on config storage:** The subsystem needs to store configs from `RegisterXxx()` until `Initialize()` runs. Add the following private maps:

```cpp
private:
    // Configs stored during registration, consumed during Initialize.
    TMap<FName, FKeyStorageConfig> StoredKeyStorageConfigs;
    TMap<FName, FString>           StoredQueryStorageConnectionStrings;
    TMap<FName, FString>           StoredAuditConnectionStrings;
    TMap<FName, FLoggingConfig>    StoredLoggingConfigs;
```

---

## RegisterXxx Methods

```cpp
void UGameCoreBackendSubsystem::RegisterKeyStorageService(
    FName Key, TScriptInterface<IKeyStorageService> Service, const FKeyStorageConfig& Config)
{
    KeyStorageServices.Add(Key, Service);
    StoredKeyStorageConfigs.Add(Key, Config);
}

// Same pattern for RegisterQueryStorageService, RegisterAuditService, RegisterLoggingService.
```

---

## Deinitialize — Ordered Shutdown

```cpp
void UGameCoreBackendSubsystem::Deinitialize()
{
    // 1. Flush all buffered logging services first (human-readable, diagnostically useful)
    for (auto& [Key, Service] : LoggingServices)
        if (ConnectedLogging.Contains(Key))
            Service.GetInterface()->Flush();

    // 2. Flush all buffered audit services
    for (auto& [Key, Service] : AuditServices)
        if (ConnectedAudit.Contains(Key))
            Service.GetInterface()->Flush();

    // 3. Unregister facade BEFORE clearing maps so GetX() routes to file-scope statics
    FGameCoreBackend::Unregister();

    // 4. Clear all maps
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

    StoredKeyStorageConfigs.Empty();
    StoredQueryStorageConnectionStrings.Empty();
    StoredAuditConnectionStrings.Empty();
    StoredLoggingConfigs.Empty();
}
```

---

## Tag Resolution

```cpp
FName UGameCoreBackendSubsystem::ResolveKeyStorageTag(FGameplayTag Tag) const
{
    const FName* Found = KeyStorageRoutes.Find(Tag);
    return Found ? *Found : NAME_None;
}
// Same pattern for ResolveQueryStorageTag(), ResolveAuditTag(), ResolveLoggingTag().
```

Resolution is **exact-match only** — O(1) `TMap` lookup. Parent-tag hierarchy is not traversed.

---

## Service Accessors — Fallback Logic

```cpp
ILoggingService* UGameCoreBackendSubsystem::GetLogging(FName Key) const
{
    const auto* Entry = LoggingServices.Find(Key);
    if (Entry && ConnectedLogging.Contains(Key))
        return Entry->GetInterface();
    return NullLogging.Get();
}
// Same pattern for GetKeyStorage(), GetQueryStorage(), GetAudit().
```

---

## Notes

- Services must be registered **before** `Super::Init()` returns, so that `Initialize()` can call `Connect()` on them.
- Unmapped tags resolve to `NAME_None`. Always register a service under `NAME_None` as the default, or all unmapped calls hit the null fallback.
- `Flush()` is called on **all connected** audit and logging services during `Deinitialize`. Key-value storage services are not flushed here — their write-behind queues are drained by their own internal threads during `Connect()` teardown.
- `friend` declarations are intentional: `Connect()` and `Initialize()` are technically public on the interfaces but must only be called by this subsystem.
