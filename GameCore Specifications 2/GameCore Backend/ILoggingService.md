# ILoggingService

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Backend/LoggingService.h`  

---

## Responsibility

Interface for forwarding server log messages to an external logging backend (Datadog, Loki, CloudWatch, etc.). The canonical logging interface for all GameCore systems — accessed via `FGameCoreBackend::Log()` or `FGameCoreBackend::GetLogging(Tag)`.

Owns its own connection lifecycle. `UGameCoreBackendSubsystem` passes a config at registration and never touches connection state again.

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
    float FlushThresholdPercent = 0.75f;

    // Maximum entries in write-behind queue before oldest entries are dropped.
    int32 MaxQueueSize = 4096;

    // Concrete implementations must split flushes into chunks of this size.
    int32 MaxBatchSize = 500;

    // Reconnect backoff (seconds).
    float ReconnectDelaySeconds    = 2.0f;
    float MaxReconnectDelaySeconds = 60.0f;
};
```

---

## Interface Declaration

```cpp
UIINTERFACE(MinimalAPI, NotBlueprintable)
class ULoggingService : public UInterface { GENERATED_BODY() };

class GAMECORE_API ILoggingService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during Initialize.
    // Service stores the config and initiates connection internally.
    // Connection is async — messages queue until connected.
    virtual void Initialize(const FLoggingConfig& Config) = 0;

    // Core method — all convenience methods route here.
    // Thread-safe: enqueues into write-behind buffer, never blocks.
    virtual void Log(
        ELogSeverity    Severity,
        const FString&  Category,
        const FString&  Message,
        const FString&  Payload = FString()) = 0;

    // Flush all buffered messages — must complete before process exit.
    // Called automatically by UGameCoreBackendSubsystem::Deinitialize().
    virtual void Flush() = 0;

    // --- Convenience methods (default-implemented) ---
    virtual void LogVerbose (const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Verbose,  Cat, Msg, P); }
    virtual void LogInfo    (const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Info,     Cat, Msg, P); }
    virtual void LogWarning (const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Warning,  Cat, Msg, P); }
    virtual void LogError   (const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Error,    Cat, Msg, P); }
    virtual void LogCritical(const FString& Cat, const FString& Msg, const FString& P = {}) { Log(ELogSeverity::Critical, Cat, Msg, P); }
};
```

---

## FLoggingServiceBase

Abstract C++ base class. Owns the write-behind queue (lock-free MPSC), flush thread, and reconnect logic. Implementors override only `ConnectToBackend` and `DispatchBatch`.

```cpp
class GAMECORE_API FLoggingServiceBase : public ILoggingService
{
public:
    virtual void Initialize(const FLoggingConfig& Config) override;
    virtual void Log(ELogSeverity Severity, const FString& Category,
                     const FString& Message, const FString& Payload) override;
    virtual void Flush() override;

protected:
    // Attempt to open connection. Return true on success.
    // Called on Initialize and on reconnect (with exponential backoff on failure).
    virtual bool ConnectToBackend(const FLoggingConfig& Config) = 0;

    // Deliver a batch to the backend. Called from the flush thread.
    // Return true on success. Respect Config.MaxBatchSize.
    virtual bool DispatchBatch(const TArray<FLogEntry>& Entries) = 0;

private:
    FLoggingConfig Config;

    // Lock-free MPSC ring buffer. Sized to Config.MaxQueueSize at Initialize time.
    // Log() enqueues from any thread; flush thread is sole consumer.
    // Oldest entries dropped silently on overflow with UE_LOG Warning.
    TCircularQueue<FLogEntry> Queue;

    FThreadSafeCounter bConnected;
    float              CurrentReconnectDelay = 0.f;

    // Dedicated platform thread — no FTimerManager dependency.
    // Handles periodic flush, pressure flush, and reconnect.
    void FlushThreadLoop();
    void AttemptReconnect();
};
```

### FLogEntry

```cpp
struct FLogEntry
{
    ELogSeverity Severity;
    FString      Category;
    FString      Message;
    FString      Payload;   // Optional. Caller-formatted (JSON, key=value, etc.)
    FDateTime    Timestamp; // Stamped at enqueue time (UTC)
};
```

### Queue Behavior

```
Log() called (any thread):
  → Enqueued into TCircularQueue — lock-free, never blocks caller
  → If depth reaches FlushThresholdPercent * MaxQueueSize:
      → Flush thread signaled for immediate dispatch (pressure flush)

Flush thread wakes (FlushIntervalSeconds):
  → Dequeues all available entries
  → Splits into MaxBatchSize chunks → DispatchBatch per chunk

MaxQueueSize exceeded:
  → Oldest entries dropped, UE_LOG Warning emitted
  → New entries continue to enqueue

Flush() — graceful shutdown:
  → Flush thread drains remaining entries synchronously before returning
  → Called automatically by UGameCoreBackendSubsystem::Deinitialize()
```

---

## Null Fallback

`FNullLoggingService` routes every call to `UE_LOG(LogGameCore, ...)`. No log message is silently dropped.

**Severity mapping:**

| `ELogSeverity` | `UE_LOG` verbosity |
|---|---|
| `Verbose` | `Verbose` |
| `Info` | `Log` |
| `Warning` | `Warning` |
| `Error` | `Error` |
| `Critical` | `Error` with `[CRITICAL]` prefix + synchronous `Flush()` |

> `Critical` does **not** map to `UE_LOG Fatal`. Fatal crashes the process and may lose buffered messages. Use `FCoreDelegates::OnHandleSystemError` if a crash is required.

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

## Notes

- `Flush()` must complete before process exit. The subsystem calls it automatically.
- `Payload` is optional and caller-formatted. Use JSON or `key=value` for structured extra data.
- `FLoggingServiceBase` runs its flush loop on a dedicated platform thread — no `FTimerManager` dependency, safe before World exists and during teardown.
- Reconnect uses exponential backoff between `ReconnectDelaySeconds` and `MaxReconnectDelaySeconds`. Messages queue during reconnect. Overflow drops oldest and emits a `UE_LOG Warning`.
- **Server-side only.**
