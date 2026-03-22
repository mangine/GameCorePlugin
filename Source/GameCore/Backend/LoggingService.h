#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Containers/CircularQueue.h"
#include "LoggingService.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGameCore, Log, All);

// ---------------------------------------------------------------------------
// ELogSeverity
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class ELogSeverity : uint8
{
    Verbose,
    Info,
    Warning,
    Error,
    Critical
};

// ---------------------------------------------------------------------------
// FLoggingConfig
// ---------------------------------------------------------------------------

USTRUCT()
struct GAMECORE_API FLoggingConfig
{
    GENERATED_BODY()

    /** Backend endpoint — format is implementation-defined (URL, DSN, host:port, etc.) */
    UPROPERTY() FString Endpoint;

    /** Interval between periodic flushes of the write-behind queue (seconds). */
    UPROPERTY() float FlushIntervalSeconds = 5.0f;

    /** Immediate flush triggered when queue reaches this fraction of MaxQueueSize. */
    UPROPERTY() float FlushThresholdPercent = 0.75f;

    /** Maximum entries in write-behind queue before oldest entries are dropped. */
    UPROPERTY() int32 MaxQueueSize = 4096;

    /** Concrete implementations must split flushes into chunks of this size. */
    UPROPERTY() int32 MaxBatchSize = 500;

    /** Reconnect backoff (seconds). */
    UPROPERTY() float ReconnectDelaySeconds    = 2.0f;
    UPROPERTY() float MaxReconnectDelaySeconds = 60.0f;
};

// ---------------------------------------------------------------------------
// FLogEntry
// ---------------------------------------------------------------------------

struct FLogEntry
{
    ELogSeverity Severity;
    FString      Category;
    FString      Message;
    FString      Payload;    // Optional. Caller-formatted (JSON, key=value, etc.)
    FDateTime    Timestamp;  // Stamped at enqueue time (UTC)
};

// ---------------------------------------------------------------------------
// ILoggingService
// ---------------------------------------------------------------------------

UINTERFACE(MinimalAPI, NotBlueprintable)
class ULoggingService : public UInterface { GENERATED_BODY() };

class GAMECORE_API ILoggingService
{
    GENERATED_BODY()

public:
    /**
     * Called only by UGameCoreBackendSubsystem during Initialize.
     * Service stores the config and initiates connection internally.
     * Connection is async — messages queue until connected.
     */
    virtual void Initialize(const FLoggingConfig& Config) = 0;

    /**
     * Core method — all convenience methods route here.
     * Thread-safe: enqueues into write-behind buffer, never blocks.
     */
    virtual void Log(
        ELogSeverity    Severity,
        const FString&  Category,
        const FString&  Message,
        const FString&  Payload = FString()) = 0;

    /**
     * Flush all buffered messages — must complete before process exit.
     * Called automatically by UGameCoreBackendSubsystem::Deinitialize().
     */
    virtual void Flush() = 0;

    // Convenience methods
    virtual void LogVerbose (const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Verbose,  Cat, Msg, P); }
    virtual void LogInfo    (const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Info,     Cat, Msg, P); }
    virtual void LogWarning (const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Warning,  Cat, Msg, P); }
    virtual void LogError   (const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Error,    Cat, Msg, P); }
    virtual void LogCritical(const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Critical, Cat, Msg, P); }

    virtual ~ILoggingService() = default;
};

// ---------------------------------------------------------------------------
// FLoggingServiceBase — Abstract base, owns write-behind queue + flush thread
// ---------------------------------------------------------------------------

/**
 * Abstract C++ base class. Owns the write-behind queue (lock-free MPSC),
 * flush thread, and reconnect logic.
 * Implementors override only ConnectToBackend and DispatchBatch.
 */
class GAMECORE_API FLoggingServiceBase : public ILoggingService, public FRunnable
{
public:
    FLoggingServiceBase();
    virtual ~FLoggingServiceBase() override;

    virtual void Initialize(const FLoggingConfig& Config) override;
    virtual void Log(ELogSeverity Severity, const FString& Category,
                     const FString& Message, const FString& Payload) override;
    virtual void Flush() override;

protected:
    /** Attempt to open connection. Return true on success. */
    virtual bool ConnectToBackend(const FLoggingConfig& Config) = 0;

    /**
     * Deliver a batch to the backend. Called from the flush thread.
     * Return true on success. Respect Config.MaxBatchSize.
     */
    virtual bool DispatchBatch(const TArray<FLogEntry>& Entries) = 0;

    // FRunnable interface
    virtual uint32 Run() override;

private:
    FLoggingConfig          Config;
    TCircularQueue<FLogEntry> Queue{ 4096 };  // Resized during Initialize
    FThreadSafeCounter      bConnected;
    FThreadSafeCounter      bShuttingDown;
    float                   CurrentReconnectDelay = 0.f;
    FRunnableThread*        FlushThread = nullptr;
    FEvent*                 FlushEvent  = nullptr;

    void AttemptReconnect();
    void DrainQueue(bool bForceSynchronous);
};

// ---------------------------------------------------------------------------
// FNullLoggingService — Routes all calls to UE_LOG
// ---------------------------------------------------------------------------

class GAMECORE_API FNullLoggingService : public ILoggingService
{
public:
    virtual void Initialize(const FLoggingConfig&) override {}
    virtual void Flush() override {}

    virtual void Log(
        ELogSeverity    Severity,
        const FString&  Category,
        const FString&  Message,
        const FString&  Payload) override
    {
        const FString Full = Payload.IsEmpty()
            ? FString::Printf(TEXT("[%s] %s"), *Category, *Message)
            : FString::Printf(TEXT("[%s] %s | %s"), *Category, *Message, *Payload);

        switch (Severity)
        {
            case ELogSeverity::Verbose:  UE_LOG(LogGameCore, Verbose, TEXT("%s"), *Full); break;
            case ELogSeverity::Info:     UE_LOG(LogGameCore, Log,     TEXT("%s"), *Full); break;
            case ELogSeverity::Warning:  UE_LOG(LogGameCore, Warning, TEXT("%s"), *Full); break;
            case ELogSeverity::Error:    UE_LOG(LogGameCore, Error,   TEXT("%s"), *Full); break;
            case ELogSeverity::Critical:
                Flush();
                UE_LOG(LogGameCore, Error, TEXT("[CRITICAL] %s"), *Full);
                break;
        }
    }
};
