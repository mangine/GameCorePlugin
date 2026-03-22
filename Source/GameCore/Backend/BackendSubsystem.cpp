#include "Backend/BackendSubsystem.h"
#include "Backend/GameCoreBackend.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
// ShouldCreateSubsystem — Server Only
// ---------------------------------------------------------------------------

bool UGameCoreBackendSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GI = Cast<UGameInstance>(Outer);
    if (!GI) return false;
    const UWorld* World = GI->GetWorld();
    if (!World) return false;
    const ENetMode NetMode = World->GetNetMode();
    return NetMode == NM_DedicatedServer || NetMode == NM_ListenServer || NetMode == NM_Standalone;
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

void UGameCoreBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Construct null fallbacks — always available, no connection required.
    NullKeyStorage   = MakeUnique<FNullKeyStorageService>();
    NullQueryStorage = MakeUnique<FNullQueryStorageService>();
    NullAudit        = MakeUnique<FNullAuditService>();
    NullLogging      = MakeUnique<FNullLoggingService>();

    // Connect KeyStorage services
    for (auto& [Key, Service] : KeyStorageServices)
    {
        if (Service->Connect(StoredKeyStorageConfigs.FindRef(Key)))
        {
            ConnectedKeyStorage.Add(Key);
        }
        else
        {
            UE_LOG(LogGameCore, Error, TEXT("[Backend] KeyStorage '%s' failed to connect."), *Key.ToString());
        }
    }

    // Connect QueryStorage services
    for (auto& [Key, Service] : QueryStorageServices)
    {
        if (Service->Connect(StoredQueryStorageConnectionStrings.FindRef(Key)))
        {
            ConnectedQueryStorage.Add(Key);
        }
        else
        {
            UE_LOG(LogGameCore, Error, TEXT("[Backend] QueryStorage '%s' failed to connect."), *Key.ToString());
        }
    }

    // Connect Audit services
    for (auto& [Key, Service] : AuditServices)
    {
        if (Service->Connect(StoredAuditConnectionStrings.FindRef(Key)))
        {
            ConnectedAudit.Add(Key);
        }
        else
        {
            UE_LOG(LogGameCore, Error, TEXT("[Backend] Audit '%s' failed to connect."), *Key.ToString());
        }
    }

    // Initialize Logging services — always considered connected after Initialize
    for (auto& [Key, Service] : LoggingServices)
    {
        Service->Initialize(StoredLoggingConfigs.FindRef(Key));
        ConnectedLogging.Add(Key);
    }

    FGameCoreBackend::Register(this);
}

// ---------------------------------------------------------------------------
// Deinitialize — Ordered Shutdown
// ---------------------------------------------------------------------------

void UGameCoreBackendSubsystem::Deinitialize()
{
    // 1. Flush all connected logging services first
    for (auto& [Key, Service] : LoggingServices)
    {
        if (ConnectedLogging.Contains(Key))
        {
            Service->Flush();
        }
    }

    // 2. Flush all connected audit services
    for (auto& [Key, Service] : AuditServices)
    {
        if (ConnectedAudit.Contains(Key))
        {
            Service->Flush();
        }
    }

    // 3. Unregister facade BEFORE clearing maps
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

    NullKeyStorage.Reset();
    NullQueryStorage.Reset();
    NullAudit.Reset();
    NullLogging.Reset();

    Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Service Registration
// ---------------------------------------------------------------------------

void UGameCoreBackendSubsystem::RegisterKeyStorageService(
    FName Key, IKeyStorageService* Service, const FKeyStorageConfig& Config)
{
    KeyStorageServices.Add(Key, Service);
    StoredKeyStorageConfigs.Add(Key, Config);
}

void UGameCoreBackendSubsystem::RegisterQueryStorageService(
    FName Key, IQueryStorageService* Service, const FString& ConnectionString)
{
    QueryStorageServices.Add(Key, Service);
    StoredQueryStorageConnectionStrings.Add(Key, ConnectionString);
}

void UGameCoreBackendSubsystem::RegisterAuditService(
    FName Key, IAuditService* Service, const FString& ConnectionString)
{
    AuditServices.Add(Key, Service);
    StoredAuditConnectionStrings.Add(Key, ConnectionString);
}

void UGameCoreBackendSubsystem::RegisterLoggingService(
    FName Key, ILoggingService* Service, const FLoggingConfig& Config)
{
    LoggingServices.Add(Key, Service);
    StoredLoggingConfigs.Add(Key, Config);
}

// ---------------------------------------------------------------------------
// Tag Routing Registration
// ---------------------------------------------------------------------------

void UGameCoreBackendSubsystem::MapTagToKeyStorage  (FGameplayTag Tag, FName Key) { KeyStorageRoutes.Add(Tag, Key); }
void UGameCoreBackendSubsystem::MapTagToQueryStorage(FGameplayTag Tag, FName Key) { QueryStorageRoutes.Add(Tag, Key); }
void UGameCoreBackendSubsystem::MapTagToAudit       (FGameplayTag Tag, FName Key) { AuditRoutes.Add(Tag, Key); }
void UGameCoreBackendSubsystem::MapTagToLogging     (FGameplayTag Tag, FName Key) { LoggingRoutes.Add(Tag, Key); }

// ---------------------------------------------------------------------------
// Tag Resolution
// ---------------------------------------------------------------------------

FName UGameCoreBackendSubsystem::ResolveKeyStorageTag  (FGameplayTag Tag) const { const FName* F = KeyStorageRoutes.Find(Tag);   return F ? *F : NAME_None; }
FName UGameCoreBackendSubsystem::ResolveQueryStorageTag(FGameplayTag Tag) const { const FName* F = QueryStorageRoutes.Find(Tag); return F ? *F : NAME_None; }
FName UGameCoreBackendSubsystem::ResolveAuditTag       (FGameplayTag Tag) const { const FName* F = AuditRoutes.Find(Tag);        return F ? *F : NAME_None; }
FName UGameCoreBackendSubsystem::ResolveLoggingTag     (FGameplayTag Tag) const { const FName* F = LoggingRoutes.Find(Tag);      return F ? *F : NAME_None; }

// ---------------------------------------------------------------------------
// FName-Based Service Accessors — Never returns nullptr
// ---------------------------------------------------------------------------

IKeyStorageService* UGameCoreBackendSubsystem::GetKeyStorage(FName Key) const
{
    const auto* Entry = KeyStorageServices.Find(Key);
    if (Entry && ConnectedKeyStorage.Contains(Key)) return *Entry;
    return NullKeyStorage.Get();
}

IQueryStorageService* UGameCoreBackendSubsystem::GetQueryStorage(FName Key) const
{
    const auto* Entry = QueryStorageServices.Find(Key);
    if (Entry && ConnectedQueryStorage.Contains(Key)) return *Entry;
    return NullQueryStorage.Get();
}

IAuditService* UGameCoreBackendSubsystem::GetAudit(FName Key) const
{
    const auto* Entry = AuditServices.Find(Key);
    if (Entry && ConnectedAudit.Contains(Key)) return *Entry;
    return NullAudit.Get();
}

ILoggingService* UGameCoreBackendSubsystem::GetLogging(FName Key) const
{
    const auto* Entry = LoggingServices.Find(Key);
    if (Entry && ConnectedLogging.Contains(Key)) return *Entry;
    return NullLogging.Get();
}
