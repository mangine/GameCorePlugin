#pragma once

/**
 * FGameCoreBackend
 *
 * Minimal stub for the GameCore backend service layer.
 * This stub routes all audit and logging calls to UE_LOG until a real backend
 * implementation is provided by the game module.
 *
 * Game modules replace this by providing a concrete IKeyStorageService and
 * IAuditService implementation and binding them before world startup.
 *
 * DEVIATION NOTE: FGameCoreBackend is referenced by Currency, Progression,
 * Loot, Inventory, Serialization, and Dialogue systems but is not formally
 * specified in GameCore Specifications 2. This stub is provided to allow
 * all systems to compile. Replace with a real implementation as needed.
 */

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGameCoreAudit, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGameCoreBackend, Log, All);

/** Minimal audit record interface — no-op stub */
struct GAMECORE_API FGameCoreAuditService
{
    /** Record an arbitrary audit event. All parameters are for future backend wiring. */
    template<typename... Args>
    void RecordEvent(Args&&...) const
    {
        // No-op in stub. Real implementation dispatches to IKeyStorageService.
    }
};

/** Minimal logging interface — routes to UE_LOG */
struct GAMECORE_API FGameCoreLoggingService
{
    void LogWarning(const FString& Message) const
    {
        UE_LOG(LogGameCoreBackend, Warning, TEXT("%s"), *Message);
    }

    void LogError(const FString& Message) const
    {
        UE_LOG(LogGameCoreBackend, Error, TEXT("%s"), *Message);
    }

    void LogInfo(const FString& Message) const
    {
        UE_LOG(LogGameCoreBackend, Log, TEXT("%s"), *Message);
    }
};

/**
 * Static accessor for GameCore backend services.
 * Returns stub implementations by default.
 * Game modules override by replacing the static pointers before world startup.
 */
struct GAMECORE_API FGameCoreBackend
{
    /**
     * Returns the audit service for the given audit tag.
     * Stub returns a no-op service. Real backend routes by tag to different audit sinks.
     */
    static FGameCoreAuditService& GetAudit(const FGameplayTag& /* AuditTag */)
    {
        static FGameCoreAuditService Stub;
        return Stub;
    }

    /**
     * Returns the logging service for the given log tag.
     * Stub routes to UE_LOG LogGameCoreBackend.
     */
    static FGameCoreLoggingService& GetLogging(const FGameplayTag& /* LogTag */)
    {
        static FGameCoreLoggingService Stub;
        return Stub;
    }
};
