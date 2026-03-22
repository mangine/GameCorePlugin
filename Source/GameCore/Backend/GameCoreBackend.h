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
 * Registered by UGameCoreBackendSubsystem::Initialize / Deinitialize.
 * Never call Register/Unregister directly.
 *
 * Delegate hooks (OnLog, OnAudit, OnPersistenceWrite) take priority over
 * the subsystem path when bound.
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
    static UGameCoreBackendSubsystem* Instance;
};
