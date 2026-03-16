# IKeyStorageService

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Backend/KeyStorageService.h`

Interface for key-value blob storage backends. GameCore is agnostic to whether the backend is RAM-based (Redis), document-based (MongoDB), or any other store. The only contract is **key-tag-binary** storage with optional TTL and server-side function execution for atomic or complex operations.

Uses `FGameplayTag` as the namespace/collection discriminator and `FGuid` as the record key. Data is exchanged as binary (`FArchive` for single ops, `TArray<uint8>` for batch ops — references cannot be stored in collections).

> Replaces `IDBService`. Rename `DBService.h` → `KeyStorageService.h` and update `UGameCoreBackendSubsystem` accordingly.

---

## Write-Behind Queue & Flush Model

`IKeyStorageService` owns a write-behind queue internally. Callers dispatch payloads via `Set` / `BatchSet` and the service manages batching, flush timing, reconnect, and retry independently. The subsystem that produces payloads (e.g. `UPersistenceSubsystem`) has no knowledge of connection state or flush schedules — it fires and forgets.

Concrete implementations must:
- Maintain an internal write-behind queue keyed by `(StorageTag, Key)`. A newer write for the same key **replaces** a pending write — never appends. This deduplicates redundant saves when the DB is temporarily unavailable.
- Flush periodically based on a configurable `FlushIntervalSeconds`.
- Trigger an **immediate flush** when queue depth reaches `FlushThresholdPercent` of `MaxQueueSize` to prevent data loss under burst load.
- Dispatch in chunks of at most `MaxBatchSize` entries per backend call.
- Implement retry with exponential backoff on dispatch failure. Retry logic, threading, and connection management are entirely the implementation's responsibility.
- Maintain two internal lanes: a **priority lane** for entries enqueued with `bCritical = true`, and a **normal lane** for all other entries. Priority lane entries are always flushed first and are never dropped on queue overflow — only normal lane entries are dropped.
- **Never block the calling thread.** All I/O, including `bFlushImmediately` dispatches, must be performed on the service's own internal thread. The calling thread (including the game thread) must return immediately from any `IKeyStorageService` call.

This design means `UPersistenceSubsystem` requires no queue, no flush timer, and no priority queue of its own. It simply calls `Set(...)` with the appropriate flags and the DB service handles everything downstream.

### Double-Write Deduplication

Because the write-behind queue is keyed by `(StorageTag, Key)`, if `UPersistenceSubsystem` dispatches a second payload for an entity before the first has been flushed to the backend (e.g. due to a DB outage spanning two save cycles), the newer payload **replaces** the pending one. This guarantees:
- No duplicate saves for the same entity.
- The backend always receives the most recent state.
- For partial saves: because partial saves cover different dirty components, a newer partial for the same entity replaces the older one. Since components are re-serialized at dispatch time, the newer partial already reflects the latest state of all dirty components at that moment.

---

## FKeyStorageConfig

Passed at registration time via `UGameCoreBackendSubsystem`. The service stores it internally.

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
    // On overflow: normal lane entries are dropped (oldest first), priority lane is never dropped.
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

Passed to all async read callbacks in place of `bool bSuccess`. Implementations must map their internal error states to one of these values.

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
UINTERFACE(MinimalAPI, NotBlueprintable)
class UKeyStorageService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IKeyStorageService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during registration.
    virtual bool Connect(const FKeyStorageConfig& Config) = 0;

    // --- Single Record Ops ---

    // Enqueue a write into the write-behind queue.
    // bFlushImmediately: bypass the queue and dispatch on the service's internal thread.
    //   The calling thread is never blocked. Use for time-critical saves (e.g. logout, zone transfer).
    // bCritical: place this entry in the priority lane — never dropped on overflow,
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

    // Returns a request handle that can be passed to Cancel().
    // The callback always fires — with Cancelled or TimedOut on failure — so callers
    // do not need to handle "callback never arrives" as a separate code path.
    // Callbacks are invoked on the game thread.
    virtual FGuid Get(
        FGameplayTag    StorageTag,
        const FGuid&    Key,
        TFunction<void(EStorageRequestResult Result, const TArray<uint8>& Data)> Callback) = 0;

    virtual void Delete(
        FGameplayTag    StorageTag,
        const FGuid&    Key) = 0;

    // --- Batch Ops ---

    // bCritical applies to all entries in the batch.
    virtual void BatchSet(
        FGameplayTag                               StorageTag,
        const TArray<TPair<FGuid, TArray<uint8>>>& Pairs,
        bool                                       bFlushImmediately = false,
        bool                                       bCritical         = false) = 0;

    // Returns a request handle that can be passed to Cancel().
    // Missing keys are absent from the result map — implementations must not error on missing keys.
    // Callbacks are invoked on the game thread.
    virtual FGuid BatchGet(
        FGameplayTag                                        StorageTag,
        const TArray<FGuid>&                               Keys,
        TFunction<void(EStorageRequestResult Result, const TMap<FGuid, TArray<uint8>>& Data)> Callback) = 0;

    virtual void BatchDelete(
        FGameplayTag         StorageTag,
        const TArray<FGuid>& Keys) = 0;

    // --- Server-Side Function Execution ---
    // For atomic or complex operations that must not be performed at game level.
    // FunctionName maps to a registered script/procedure on the backend (e.g. a Lua
    // script in Redis, a stored procedure in a document store).
    // Args and result are serialized as JSON strings.
    // Returns a request handle that can be passed to Cancel().
    // Callbacks are invoked on the game thread.
    virtual FGuid ExecuteFunction(
        FGameplayTag    StorageTag,
        const FString&  FunctionName,
        const FString&  ArgsJson,
        TFunction<void(EStorageRequestResult Result, const FString& ResultJson)> Callback) = 0;

    // --- Cancellation ---

    // Cancel a pending Get, BatchGet, or ExecuteFunction request.
    // If the request has already completed, this is a no-op.
    // The callback will fire with EStorageRequestResult::Cancelled if cancellation succeeds.
    // Implementations are not required to abort in-flight network calls —
    // cancellation prevents the callback from being acted on, not necessarily the wire call.
    virtual void Cancel(FGuid RequestHandle) = 0;
};
```

---

## Null Fallback Implementation

Used automatically by `UGameCoreBackendSubsystem` when no service is registered or connection failed.

```cpp
class GAMECORE_API FNullKeyStorageService : public IKeyStorageService
{
public:
    virtual bool Connect(const FKeyStorageConfig&) override { return true; }

    virtual void Set(FGameplayTag, const FGuid& Key, const TArray<uint8>&, bool, bool) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Set called for key %s — no backend connected."),
            *Key.ToString());
    }

    virtual void SetWithTTL(FGameplayTag, const FGuid& Key, const TArray<uint8>&, int32, bool, bool) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] SetWithTTL called for key %s — no backend connected."),
            *Key.ToString());
    }

    virtual FGuid Get(FGameplayTag, const FGuid& Key,
        TFunction<void(EStorageRequestResult, const TArray<uint8>&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Get called for key %s — returning failure."),
            *Key.ToString());
        Callback(EStorageRequestResult::Failure, {});
        return FGuid{};
    }

    virtual void Delete(FGameplayTag, const FGuid& Key) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Delete called for key %s — no backend connected."),
            *Key.ToString());
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

    virtual FGuid ExecuteFunction(FGameplayTag, const FString& FunctionName,
        const FString&,
        TFunction<void(EStorageRequestResult, const FString&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] ExecuteFunction '%s' called — no backend connected."),
            *FunctionName);
        Callback(EStorageRequestResult::Failure, TEXT("{}"));
        return FGuid{};
    }

    virtual void Cancel(FGuid) override
    {
        // No-op — null service has no pending requests.
    }
};
```

---

## Wiring with UPersistenceSubsystem

`UPersistenceSubsystem` dispatches `FEntityPersistencePayload` via tag delegates. The game module binds those delegates and forwards to `IKeyStorageService::Set`. The subsystem passes `bFlushImmediately` and `bCritical` based on `ESerializationReason` — the DB service handles everything downstream.

```cpp
void UMyGameServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UPersistenceSubsystem>();
    Collection.InitializeDependency<UGameCoreBackendSubsystem>();

    auto* Persistence = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>();
    auto* Backend     = GetGameInstance()->GetSubsystem<UGameCoreBackendSubsystem>();

    Persistence->RegisterPersistenceTag(TAG_Persistence_Entity_Player);
    Persistence->GetSaveDelegate(TAG_Persistence_Entity_Player)
        ->AddLambda([Backend](const FEntityPersistencePayload& Payload)
        {
            TArray<uint8> Bytes;
            FMemoryWriter Writer(Bytes);
            // Serialize Payload.Components into Writer...

            const bool bCritical         = Payload.bCritical;
            const bool bFlushImmediately = Payload.bFlushImmediately;

            Backend->GetKeyStorage(TEXT("PlayerDB"))->Set(
                Payload.PersistenceTag,
                Payload.EntityId,
                Bytes,
                bFlushImmediately,
                bCritical);
        });
}
```

---

## Cancellation Pattern

Store the request handle when cancellation may be needed (e.g. login, zone transfer). If the requesting object is destroyed before the backend responds, call `Cancel` then let the object die — the callback will not fire.

```cpp
// In a session manager or login handler:
FGuid LoadHandle = Storage->Get(TAG_Persistence_Player, PlayerId,
    [this, WeakSelf = TWeakObjectPtr<UMySessionManager>(this)]
    (EStorageRequestResult Result, const TArray<uint8>& Data)
    {
        if (!WeakSelf.IsValid()) return;

        if (Result == EStorageRequestResult::Success)
        {
            // Deserialize and continue login flow.
        }
        else
        {
            // Result is Failure, Cancelled, or TimedOut — treat as session-breaking event.
            UE_LOG(LogGame, Error, TEXT("Player load failed: %s"),
                *UEnum::GetValueAsString(Result));
            AbortLogin();
        }
    });

// On session manager destruction or login abort:
Storage->Cancel(LoadHandle);
```

---

## TTL Use Cases

`SetWithTTL` is intended for ephemeral data that must not persist beyond a session or window:

| Use Case | Example TTL |
|---|---|
| Session locks | 30–60 s |
| Market reservation hold | 300 s |
| Temporary spawn state | Session lifetime |

---

## ExecuteFunction Contract

`ExecuteFunction` maps to a backend-native callable — a Lua script in Redis, a stored procedure in a document store. It is **not** for game logic. Use it for:

- Atomic compare-and-swap on inventory slots
- Conditional expiry resets
- Aggregated counters that must not race

Args and result use JSON strings. The implementation is responsible for type mapping.

---

## Notes

- `StorageTag` serves as namespace/collection discriminator. Implementations may route to different collections, key prefixes, or Redis databases.
- `Connect()` is public for interface technical reasons but **must only be called by `UGameCoreBackendSubsystem`**.
- All async callbacks (`Get`, `BatchGet`, `ExecuteFunction`) are always invoked — callers never need to handle "callback never arrives." On cancellation or timeout the callback fires with the appropriate `EStorageRequestResult`.
- All callbacks are invoked on the game thread. Implementations must marshal results from their internal thread before invoking.
- `BatchGet` returns only found keys. Missing GUIDs are absent from the result map — implementations must not error on missing keys.
- `Delete` and `BatchDelete` are fire-and-forget. If confirmation is needed, use `ExecuteFunction` with a conditional delete script.
- The write-behind queue is keyed by `(StorageTag, Key)` — newer writes replace pending writes for the same key. This is the correct behavior for entity persistence: we always want the most recent state dispatched to the backend, never duplicates.
- `bCritical = true` entries are never dropped on queue overflow and are always flushed before normal entries. Use for logout, zone transfer, and server shutdown saves.
- `bFlushImmediately = true` dispatches on the service's internal thread — the calling thread is never blocked. Implementations must assert this if in doubt: `check(!IsInGameThread())` inside the flush dispatch path.
- Cancellation does not guarantee the wire call is aborted — it guarantees the callback is suppressed. Implementations may abort in-flight calls as an optimization.
- If the request handle from `Get`/`BatchGet`/`ExecuteFunction` is discarded, cancellation is simply unavailable for that call. This is acceptable for fire-and-forget reads where the caller does not need to cancel.
