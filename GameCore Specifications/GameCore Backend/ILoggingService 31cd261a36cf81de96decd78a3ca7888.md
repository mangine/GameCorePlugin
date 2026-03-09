# ILoggingService

**Module:** `GameCore`

**Location:** `GameCore/Source/GameCore/Backend/LoggingService.h`

Interface for forwarding server log messages to an external logging backend (e.g. Datadog, Loki, CloudWatch). This is the **canonical logging interface** for GameCore systems. The temporary `ILogSubsystem` previously referenced in `UPersistenceSubsystem` is superseded by this interface via `UGameCoreBackendSubsystem`.

Convenience methods (`LogInfo`, `LogWarning`, etc.) are default-implemented via `Log()` so implementors only need to override one method.

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

## Interface Declaration

```cpp
UINTERFACE(MinimalAPI, NotBlueprintable)
class ULoggingService : public UInterface { GENERATED_BODY() };

class GAMECORE_API ILoggingService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during registration
    virtual bool Connect(const FString& ConnectionString) = 0;

    // Core method — all convenience methods route here
    virtual void Log(
        ELogSeverity    Severity,
        const FString&  Category,
        const FString&  Message) = 0;

    // Flush buffered messages — must be called on graceful shutdown
    virtual void Flush() = 0;

    // --- Convenience methods (default-implemented) ---
    virtual void LogVerbose (const FString& Category, const FString& Message) { Log(ELogSeverity::Verbose,  Category, Message); }
    virtual void LogInfo    (const FString& Category, const FString& Message) { Log(ELogSeverity::Info,     Category, Message); }
    virtual void LogWarning (const FString& Category, const FString& Message) { Log(ELogSeverity::Warning,  Category, Message); }
    virtual void LogError   (const FString& Category, const FString& Message) { Log(ELogSeverity::Error,    Category, Message); }
    virtual void LogCritical(const FString& Category, const FString& Message) { Log(ELogSeverity::Critical, Category, Message); }
};
```

---

## Null Fallback Implementation

Routes all messages to `UE_LOG`. Used automatically when no service is registered or connection failed.

```cpp
class GAMECORE_API FNullLoggingService : public ILoggingService
{
public:
    virtual bool Connect(const FString&) override { return true; }
    virtual void Flush() override {}

    virtual void Log(
        ELogSeverity    Severity,
        const FString&  Category,
        const FString&  Message) override
    {
        switch (Severity)
        {
            case ELogSeverity::Verbose:
                UE_LOG(LogGameCore, Verbose,  TEXT("[%s] %s"), *Category, *Message); break;
            case ELogSeverity::Info:
                UE_LOG(LogGameCore, Log,      TEXT("[%s] %s"), *Category, *Message); break;
            case ELogSeverity::Warning:
                UE_LOG(LogGameCore, Warning,  TEXT("[%s] %s"), *Category, *Message); break;
            case ELogSeverity::Error:
                UE_LOG(LogGameCore, Error,    TEXT("[%s] %s"), *Category, *Message); break;
            case ELogSeverity::Critical:
                UE_LOG(LogGameCore, Fatal,    TEXT("[%s] %s"), *Category, *Message); break;
        }
    }
};
```

---

## Wiring with UPersistenceSubsystem

`UPersistenceSubsystem` currently calls `ILogSubsystem` directly. To migrate it to `ILoggingService`, replace the internal interface call site with a lookup through `UGameCoreBackendSubsystem`.

```cpp
// Example: replacing ILogSubsystem usage in UPersistenceSubsystem::MoveToSaveQueue
// BEFORE (old ILogSubsystem)
if (auto* Log = GetGameInstance()->GetSubsystem<ILogSubsystem>())
    Log->LogWarning(TEXT("Persistence"), Message);

// AFTER (ILoggingService via UGameCoreBackendSubsystem)
// Always safe — GetLogging() never returns null, falls back to UE_LOG if not connected
if (auto* Backend = GetGameInstance()->GetSubsystem<UGameCoreBackendSubsystem>())
    Backend->GetLogging()->LogWarning(TEXT("Persistence"), Message);
```

> Wiring is opt-in. `UPersistenceSubsystem` does not need to depend on `UGameCoreBackendSubsystem`. Only wire when a real logging backend is available.
> 

---

## Notes

- `Flush()` **must** be called during graceful server shutdown before `Deinitialize`. The subsystem calls it automatically in `Deinitialize`.
- `LogCritical` maps to `UE_LOG Fatal` in the null implementation, which crashes the process. Implementations should treat `Critical` as a fatal signal and ensure the message is durably written before any crash handling.
- `Connect()` is public for interface technical reasons but **must only be called by `UGameCoreBackendSubsystem`**.