# ILoggingService

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Backend/LoggingService.h`

Interface for forwarding server log messages to an external logging backend (e.g. Datadog, Loki, CloudWatch). This is the **canonical logging interface** for GameCore systems. The temporary `ILogSubsystem` previously referenced in `UPersistenceSubsystem` is superseded by this interface via `UGameCoreBackendSubsystem`.

Convenience methods (`LogInfo`, `LogWarning`, etc.) are default-implemented via `Log()` so implementors only need to override one method.

The service owns its own connection lifecycle. `UGameCoreBackendSubsystem` acts as a registry only — it passes a config struct at registration time and never touches connection state again. The service is responsible for initial connection, reconnection, and backoff internally.

---

## ELogSeverity

```cpp
UENUM()
enum class ELogSeverity : uint8
{
    Verbose,
    Info,
    Warning,
    Error,
    Critical
};
```

---

## FLoggingConfig

Passed at registration time. The service stores it internally and uses it for the initial connection and any reconnection attempts.

```cpp
USTRUCT()
struct GAMECORE_API FLoggingConfig
{
    GENERATED_BODY()

    // Backend endpoint — format is implementation-defined (URL, DSN, host:port, etc.)
    FString Endpoint;

    // Interval between periodic flushes of the write-behind queue (seconds).
    float FlushIntervalSeconds = 5.0f;

    // Immediate flush triggered when queue reaches this fraction of MaxQueueSize.
    // Prevents data loss under burst load at the cost of a spike.
    float FlushThresholdPercent = 0.75f;

    // Maximum entries in the write-behind queue before oldest entries are dropped.
    int32 MaxQueueSize = 4096;

    // Implementation hint: concrete implementations must split flushes into chunks of this size.
    int32 MaxBatchSize = 500;

    // Reconnect backoff: initial delay (seconds). Doubles on each failed attempt up to MaxReconnectDelaySeconds.
    float ReconnectDelaySeconds    = 2.0f;
    float MaxReconnectDelaySeconds = 60.0f;
};
```

---

## Interface Declaration

```cpp
UINTERFACE(MinimalAPI, NotBlueprintable)
class ULoggingService : public UInterface { GENERATED_BODY() };

class GAMECORE_API ILoggingService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during registration.
    // The service stores the config and initiates the connection internally.
    // Connection is async — the service queues messages until connected.
    virtual void Initialize(const FLoggingConfig& Config) = 0;

    // Core method — all convenience methods route here.
    // Thread-safe: enqueues into the write-behind buffer, never blocks.
    virtual void Log(
        ELogSeverity    Severity,
        const FString&  Category,
        const FString&  Message,
        const FString&  Payload = FString()) = 0;

    // Flush all buffered messages synchronously — must complete before process exit.
    // Called automatically by UGameCoreBackendSubsystem::Deinitialize().
    virtual void Flush() = 0;

    // --- Convenience methods (default-implemented) ---
    virtual void LogVerbose (const FString& Category, const FString& Message, const FString& Payload = {}) { Log(ELogSeverity::Verbose,  Category, Message, Payload); }
    virtual void LogInfo    (const FString& Category, const FString& Message, const FString& Payload = {}) { Log(ELogSeverity::Info,     Category, Message, Payload); }
    virtual void LogWarning (const FString& Category, const FString& Message, const FString& Payload = {}) { Log(ELogSeverity::Warning,  Category, Message, Payload); }
    virtual void LogError   (const FString& Category, const FString& Message, const FString& Payload = {}) { Log(ELogSeverity::Error,    Category, Message, Payload); }
    virtual void LogCritical(const FString& Category, const FString& Message, const FString& Payload = {}) { Log(ELogSeverity::Critical, Category, Message, Payload); }
};
```

---

## FLoggingServiceBase

Abstract C++ base class that all concrete logging backends extend. Owns the write-behind queue, flush timer, and reconnect logic. Implementors only override `ConnectToBackend` and `DispatchBatch`.

The write-behind queue uses `TCircularQueue` for lock-free MPSC (multi-producer, single-consumer) access. `Log()` can be safely called from any thread without blocking the caller. The dedicated flush thread is the sole consumer.

```cpp
class GAMECORE_API FLoggingServiceBase : public ILoggingService
{
public:
    // --- ILoggingService ---
    virtual void Initialize(const FLoggingConfig& Config) override;
    virtual void Log(ELogSeverity Severity, const FString& Category, const FString& Message, const FString& Payload) override;
    virtual void Flush() override;

protected:
    // Attempt to open a connection to the backend. Called on Initialize and on reconnect.
    // Return true if connection succeeded. On failure, FLoggingServiceBase retries with exponential backoff.
    virtual bool ConnectToBackend(const FLoggingConfig& Config) = 0;

    // Deliver a batch of entries to the backend. Called from the flush thread.
    // Return true on success.
    // Implementations must respect Config.MaxBatchSize — split large flushes into multiple calls.
    // Retry logic, connection state, and threading are the responsibility of the concrete class.
    virtual bool DispatchBatch(const TArray<FLogEntry>& Entries) = 0;

private:
    FLoggingConfig Config;

    // Lock-free MPSC ring buffer. Sized to Config.MaxQueueSize at Initialize time.
    // Log() enqueues from any thread; the flush thread is the sole consumer.
    // Oldest entries are dropped silently on overflow with a UE_LOG Warning.
    TCircularQueue<FLogEntry> Queue;

    FThreadSafeCounter bConnected;
    float              CurrentReconnectDelay = 0.f;

    // Runs on a dedicated platform thread — no FTimerManager dependency.
    // Handles periodic flush, pressure flush at FlushThresholdPercent, and reconnect.
    void FlushThreadLoop();
    void AttemptReconnect();
};
```

### FLogEntry

Internal entry stored in the write-behind queue.

```cpp
struct FLogEntry
{
    ELogSeverity Severity;
    FString      Category;
    FString      Message;
    FString      Payload;   // Optional. Caller-formatted extra data (JSON, key=value, etc.)
    FDateTime    Timestamp; // Stamped at enqueue time (UTC).
};
```

### Queue Behavior

```
Log() called (any thread):
  → Enqueued into TCircularQueue — lock-free, never blocks caller
  → If queue depth reaches FlushThresholdPercent of MaxQueueSize:
      → Flush thread signaled for immediate dispatch (pressure flush)

Flush thread wakes periodically (FlushIntervalSeconds):
  → Dequeues all available entries
  → Splits into batches of MaxBatchSize → DispatchBatch per batch

MaxQueueSize exceeded:
  → Oldest entries dropped, UE_LOG Warning emitted
  → New entries continue to enqueue

Flush() (graceful shutdown):
  → Flush thread drains remaining entries synchronously before returning
  → Called automatically by UGameCoreBackendSubsystem::Deinitialize()
```

---

## Null Fallback Implementation

Routes all messages to `UE_LOG`. Used automatically when no service is registered.

```cpp
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
```

---

## Wiring with UGameCoreBackendSubsystem

```cpp
FLoggingConfig Config;
Config.Endpoint              = TEXT("https://logs.datadoghq.com/...");
Config.FlushIntervalSeconds  = 5.0f;
Config.FlushThresholdPercent = 0.75f;
Config.MaxQueueSize          = 4096;
Config.MaxBatchSize          = 500;

Backend->RegisterLoggingService(NAME_None, MyLoggingService, Config);

// Always safe — GetLogging() never returns null, falls back to FNullLoggingService if not registered
Backend->GetLogging()->LogWarning(TEXT("Persistence"), Message);
```

---

## Notes

- `Flush()` **must** complete before process exit. The subsystem calls it automatically in `Deinitialize`.
- `LogCritical` never maps to `UE_LOG Fatal`. Fatal crashes the process and may lose buffered messages. Use a separate crash handler (e.g. `FCoreDelegates::OnHandleSystemError`) if a process crash is required after a critical log.
- `Payload` is optional and caller-formatted. Use JSON or `key=value` pairs for structured extra data (e.g. entity IDs, request context). The logging backend indexes it as-is — no parsing is done by the service.
- `FLoggingServiceBase` runs its flush loop on a dedicated platform thread, not via `FTimerManager`, so it is safe to use before a World exists and during teardown.
- Reconnect uses exponential backoff between `ReconnectDelaySeconds` and `MaxReconnectDelaySeconds`. Messages continue queuing during reconnect. If `MaxQueueSize` is exceeded, the oldest entries are dropped and a `UE_LOG Warning` is emitted.
- **Abstraction vs implementation:** `FLoggingServiceBase` defines queue ownership, the pressure-flush mechanism, and the flush thread contract. Retry logic, connection pooling, and transport details are the responsibility of the **concrete implementation**. `MaxBatchSize` is a configuration hint — concrete implementations must respect it by splitting large flushes into multiple `DispatchBatch` calls.
- This interface is **server-side only**. Never instantiate or call from client code.
