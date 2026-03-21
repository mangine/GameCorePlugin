# IKeyStorageService

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Backend/KeyStorageService.h`  

---

## Responsibility

Interface for key-value blob storage backends (Redis, MongoDB, etc.). Uses `FGameplayTag` as the namespace/collection discriminator and `FGuid` as the record key. Data is exchanged as `TArray<uint8>` binary blobs.

Owns a write-behind queue internally. Callers fire-and-forget via `Set` / `BatchSet`; the service manages batching, flush timing, reconnect, and retry. `UPersistenceSubsystem` needs no queue of its own.

---

## FKeyStorageConfig

```cpp
USTRUCT()
struct GAMECORE_API FKeyStorageConfig
{
    GENERATED_BODY()

    // Backend endpoint — format is implementation-defined.
    FString ConnectionString;

    // Interval between periodic flushes of the write-behind queue (seconds).
    float FlushIntervalSeconds = 5.0f;

    // Immediate flush triggered when queue reaches this fraction of MaxQueueSize.
    float FlushThresholdPercent = 0.75f;

    // Hard cap on write-behind queue size.
    // On overflow: normal lane entries are dropped (oldest first).
    // Priority lane entries (bCritical = true) are NEVER dropped.
    int32 MaxQueueSize = 5000;

    // Maximum number of entries dispatched per backend call.
    int32 MaxBatchSize = 200;

    // Reconnect backoff.
    float ReconnectDelaySeconds    = 2.0f;
    float MaxReconnectDelaySeconds = 60.0f;
};
```

---

## EStorageRequestResult

```cpp
enum class EStorageRequestResult : uint8
{
    Success,    // Data returned successfully.
    Failure,    // Backend error or key not found.
    Cancelled,  // Cancel() was called for this request handle before the backend responded.
    TimedOut,   // Implementation-defined timeout elapsed before the backend responded.
};
```

---

## Interface Declaration

```cpp
UIINTERFACE(MinimalAPI, NotBlueprintable)
class UKeyStorageService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IKeyStorageService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during Initialize.
    virtual bool Connect(const FKeyStorageConfig& Config) = 0;

    // -------------------------------------------------------------------------
    // Single Record Ops
    // -------------------------------------------------------------------------

    // Enqueue a write into the write-behind queue.
    // bFlushImmediately: bypass the queue and dispatch on the service's internal thread.
    //   The calling thread is NEVER blocked.
    // bCritical: place in the priority lane — never dropped on overflow,
    //   always flushed before normal entries.
    virtual void Set(
        FGameplayTag         StorageTag,
        const FGuid&         Key,
        const TArray<uint8>& Data,
        bool                 bFlushImmediately = false,
        bool                 bCritical         = false) = 0;

    virtual void SetWithTTL(
        FGameplayTag         StorageTag,
        const FGuid&         Key,
        const TArray<uint8>& Data,
        int32                TTLSeconds,
        bool                 bFlushImmediately = false,
        bool                 bCritical         = false) = 0;

    // Returns a request handle for Cancel().
    // Callback always fires — with Cancelled or TimedOut on failure.
    // Callbacks are invoked on the game thread.
    virtual FGuid Get(
        FGameplayTag    StorageTag,
        const FGuid&    Key,
        TFunction<void(EStorageRequestResult Result, const TArray<uint8>& Data)> Callback) = 0;

    virtual void Delete(
        FGameplayTag    StorageTag,
        const FGuid&    Key) = 0;

    // -------------------------------------------------------------------------
    // Batch Ops
    // -------------------------------------------------------------------------

    // bCritical applies to all entries in the batch.
    virtual void BatchSet(
        FGameplayTag                               StorageTag,
        const TArray<TPair<FGuid, TArray<uint8>>>& Pairs,
        bool                                       bFlushImmediately = false,
        bool                                       bCritical         = false) = 0;

    // Missing keys are absent from the result map — not an error.
    // Callback always fires. Returns request handle for Cancel().
    // Callbacks are invoked on the game thread.
    virtual FGuid BatchGet(
        FGameplayTag                        StorageTag,
        const TArray<FGuid>&                Keys,
        TFunction<void(EStorageRequestResult Result, const TMap<FGuid, TArray<uint8>>& Data)> Callback) = 0;

    virtual void BatchDelete(
        FGameplayTag         StorageTag,
        const TArray<FGuid>& Keys) = 0;

    // -------------------------------------------------------------------------
    // Server-Side Function Execution
    // -------------------------------------------------------------------------
    // Maps to a backend-native callable (Redis Lua script, stored procedure, etc.).
    // Args and result are JSON strings.
    // Returns request handle for Cancel().
    // Callbacks are invoked on the game thread.
    virtual FGuid ExecuteFunction(
        FGameplayTag    StorageTag,
        const FString&  FunctionName,
        const FString&  ArgsJson,
        TFunction<void(EStorageRequestResult Result, const FString& ResultJson)> Callback) = 0;

    // -------------------------------------------------------------------------
    // Cancellation
    // -------------------------------------------------------------------------
    // Cancel a pending Get, BatchGet, or ExecuteFunction.
    // No-op if the request has already completed.
    // The callback fires with EStorageRequestResult::Cancelled if cancellation succeeds.
    // Implementations are not required to abort the in-flight wire call.
    virtual void Cancel(FGuid RequestHandle) = 0;
};
```

---

## Write-Behind Queue Contract

Concrete implementations must:
- Maintain an internal write-behind queue keyed by `(StorageTag, Key)`. A newer write for the same key **replaces** any pending write — deduplicates redundant saves.
- Flush periodically based on `FlushIntervalSeconds`.
- Trigger an immediate flush when queue depth reaches `FlushThresholdPercent * MaxQueueSize`.
- Dispatch in chunks of at most `MaxBatchSize` per backend call.
- Maintain two lanes: **priority** (`bCritical=true`) and **normal**. Priority lane is never dropped; normal lane oldest entries are dropped on `MaxQueueSize` overflow.
- **Never block the calling thread** — all I/O, including `bFlushImmediately` dispatches, runs on the service's own internal thread.
- Implement retry with exponential backoff on dispatch failure.

---

## Null Fallback

```cpp
class GAMECORE_API FNullKeyStorageService : public IKeyStorageService
{
public:
    virtual bool Connect(const FKeyStorageConfig&) override { return true; }

    virtual void Set(FGameplayTag, const FGuid& Key, const TArray<uint8>&, bool, bool) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Set called for key %s — no backend connected."), *Key.ToString());
    }

    virtual void SetWithTTL(FGameplayTag, const FGuid& Key, const TArray<uint8>&, int32, bool, bool) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] SetWithTTL called for key %s — no backend connected."), *Key.ToString());
    }

    virtual FGuid Get(FGameplayTag, const FGuid& Key,
        TFunction<void(EStorageRequestResult, const TArray<uint8>&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Get called for key %s — returning failure."), *Key.ToString());
        Callback(EStorageRequestResult::Failure, {});
        return FGuid{};
    }

    virtual void Delete(FGameplayTag, const FGuid& Key) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Delete called for key %s — no backend connected."), *Key.ToString());
    }

    virtual void BatchSet(FGameplayTag, const TArray<TPair<FGuid, TArray<uint8>>>&, bool, bool) override
    {
        UE_LOG(LogGameCore, Warning, TEXT("[NullKeyStorage] BatchSet called — no backend connected."));
    }

    virtual FGuid BatchGet(FGameplayTag, const TArray<FGuid>&,
        TFunction<void(EStorageRequestResult, const TMap<FGuid, TArray<uint8>>&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning, TEXT("[NullKeyStorage] BatchGet called — returning empty results."));
        Callback(EStorageRequestResult::Failure, {});
        return FGuid{};
    }

    virtual void BatchDelete(FGameplayTag, const TArray<FGuid>&) override
    {
        UE_LOG(LogGameCore, Warning, TEXT("[NullKeyStorage] BatchDelete called — no backend connected."));
    }

    virtual FGuid ExecuteFunction(FGameplayTag, const FString& FunctionName, const FString&,
        TFunction<void(EStorageRequestResult, const FString&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] ExecuteFunction '%s' called — no backend connected."), *FunctionName);
        Callback(EStorageRequestResult::Failure, TEXT("{}"));
        return FGuid{};
    }

    virtual void Cancel(FGuid) override {}
};
```

---

## TTL Use Cases

| Use Case | Typical TTL |
|---|---|
| Session locks | 30–60 s |
| Market reservation hold | 300 s |
| Temporary spawn state | Session lifetime |

---

## ExecuteFunction Contract

Use for atomic or complex operations that must not race at game level:
- Atomic inventory slot compare-and-swap
- Conditional expiry resets
- Aggregated counters

Args and result are JSON strings. Implementation is responsible for type mapping to the native backend.

---

## Notes

- `StorageTag` serves as namespace/collection discriminator. Implementations may route to different collections, key prefixes, or Redis databases.
- `Connect()` must only be called by `UGameCoreBackendSubsystem`.
- All async callbacks (`Get`, `BatchGet`, `ExecuteFunction`) **always** fire — callers never handle "callback never arrives."
- All callbacks are invoked on the **game thread**. Implementations must marshal from their internal thread.
- `BatchGet` returns only found keys. Missing GUIDs are absent from the result map.
- Write-behind queue is keyed by `(StorageTag, Key)` — newer writes replace pending writes for the same key. Always dispatches most-recent state.
- `bFlushImmediately` dispatches on the service's internal thread — the calling thread is never blocked. Implementations may assert `check(!IsInGameThread())` inside the dispatch path.
- Cancellation guarantees the callback is suppressed, not necessarily that the wire call is aborted.
