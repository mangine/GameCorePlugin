#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Backend/LoggingService.h"
#include "Backend/KeyStorageService.h"
#include "Backend/QueryStorageService.h"
#include "Backend/AuditService.h"
#include "BackendSubsystem.generated.h"

/**
 * Lifecycle manager for all backend service instances.
 *
 * Owns registration maps, connected-state sets, and tag routing tables.
 * Calls Connect() / Initialize() on each service during Initialize().
 * Calls Flush() on logging and audit services during Deinitialize().
 * Registers and unregisters FGameCoreBackend facade.
 *
 * Direct access by plugin systems is NOT permitted. Use FGameCoreBackend exclusively.
 *
 * Server-only: ShouldCreateSubsystem returns false on clients.
 */
UCLASS()
class GAMECORE_API UGameCoreBackendSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // -------------------------------------------------------------------------
    // Service Registration
    // Call from GameInstance::Init, BEFORE Super::Init().
    // -------------------------------------------------------------------------
    void RegisterKeyStorageService  (FName Key, IKeyStorageService*   Service, const FKeyStorageConfig& Config);
    void RegisterQueryStorageService(FName Key, IQueryStorageService* Service, const FString& ConnectionString);
    void RegisterAuditService       (FName Key, IAuditService*        Service, const FString& ConnectionString);
    void RegisterLoggingService     (FName Key, ILoggingService*      Service, const FLoggingConfig& Config);

    // -------------------------------------------------------------------------
    // Tag Routing Registration
    // Map FGameplayTag -> FName. Unmapped tags resolve to NAME_None.
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
    // FName-Based Accessors — used internally by FGameCoreBackend only.
    // Never returns nullptr.
    // -------------------------------------------------------------------------
    IKeyStorageService*   GetKeyStorage  (FName Key = NAME_None) const;
    IQueryStorageService* GetQueryStorage(FName Key = NAME_None) const;
    IAuditService*        GetAudit       (FName Key = NAME_None) const;
    ILoggingService*      GetLogging     (FName Key = NAME_None) const;

private:
    // --- Service Maps (raw pointers; game module owns lifetime) ---
    TMap<FName, IKeyStorageService*>   KeyStorageServices;
    TMap<FName, IQueryStorageService*> QueryStorageServices;
    TMap<FName, IAuditService*>        AuditServices;
    TMap<FName, ILoggingService*>      LoggingServices;

    // Tracks which services successfully connected
    TSet<FName> ConnectedKeyStorage;
    TSet<FName> ConnectedQueryStorage;
    TSet<FName> ConnectedAudit;
    TSet<FName> ConnectedLogging;

    // --- Tag Routing Maps ---
    TMap<FGameplayTag, FName> KeyStorageRoutes;
    TMap<FGameplayTag, FName> QueryStorageRoutes;
    TMap<FGameplayTag, FName> AuditRoutes;
    TMap<FGameplayTag, FName> LoggingRoutes;

    // --- Configs stored during registration, consumed during Initialize ---
    TMap<FName, FKeyStorageConfig> StoredKeyStorageConfigs;
    TMap<FName, FString>           StoredQueryStorageConnectionStrings;
    TMap<FName, FString>           StoredAuditConnectionStrings;
    TMap<FName, FLoggingConfig>    StoredLoggingConfigs;

    // --- Null Fallbacks (heap-allocated, outlive any accessor call during teardown) ---
    TUniquePtr<FNullKeyStorageService>   NullKeyStorage;
    TUniquePtr<FNullQueryStorageService> NullQueryStorage;
    TUniquePtr<FNullAuditService>        NullAudit;
    TUniquePtr<FNullLoggingService>      NullLogging;
};
