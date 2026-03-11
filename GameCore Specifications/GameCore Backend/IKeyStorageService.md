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
    // bFlushImmediately: bypass the queue and dispatch to the backend synchronously.
    //   Use for time-critical single-entity saves (e.g. logout, zone transfer).
    // bCritical: place this entry in the priority lane — never dropped on overflow,
    //   always flushed before normal entries.
    virtual void Set(
        FGameplayTag        StorageTag,
        const FGuid&        Key,
        const TArray<uint8>& Data,
        bool                bFlushImmediately = false,
        bool                bCritical         = false) = 0;

    virtual void SetWithTTL(
        FGameplayTag        StorageTag,
        const FGuid&        Key,
        const TArray<uint8>& Data,
        int32               TTLSeconds,
        bool                bFlushImmediately = false,
        bool                bCritical         = false) = 0;

    virtual void Get(
        FGameplayTag    StorageTag,
        const FGuid&    Key,
        TFunction<void(bool bSuccess, const TArray<uint8>& Data)> Callback) = 0;

    virtual void Delete(
        FGameplayTag    StorageTag,
        const FGuid&    Key) = 0;

    // --- Batch Ops ---

    // bCritical applies to all entries in the batch.
    virtual void BatchSet(
        FGameplayTag                                        StorageTag,
        const TArray<TPair<FGuid, TArray<uint8>>>&          Pairs,
        bool                                               bFlushImmediately = false,
        bool                                               bCritical         = false) = 0;

    virtual void BatchGet(
        FGameplayTag                                        StorageTag,
        const TArray<FGuid>&                               Keys,
        TFunction<void(const TMap<FGuid, TArray<uint8>>&)> Callback) = 0;

    virtual void BatchDelete(
        FGameplayTag            StorageTag,
        const TArray<FGuid>&    Keys) = 0;

    // --- Server-Side Function Execution ---
    // For atomic or complex operations that must not be performed at game level.
    // FunctionName maps to a registered script/procedure on the backend (e.g. a Lua
    // script in Redis, a stored procedure in a document store).
    // Args and result are serialized as JSON strings.
    virtual void ExecuteFunction(
        FGameplayTag            StorageTag,
        const FString&          FunctionName,
        const FString&          ArgsJson,
        TFunction<void(bool bSuccess, const FString& ResultJson)> Callback) = 0;
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

    virtual void Get(FGameplayTag, const FGuid& Key,
        TFunction<void(bool, const TArray<uint8>&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Get called for key %s — returning failure."),
            *Key.ToString());
        Callback(false, {});
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

    virtual void BatchGet(FGameplayTag, const TArray<FGuid>&,
        TFunction<void(const TMap<FGuid, TArray<uint8>>&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning, TEXT("[NullKeyStorage] BatchGet called — returning empty results."));
        Callback({});
    }

    virtual void BatchDelete(FGameplayTag, const TArray<FGuid>&) override
    {
        UE_LOG(LogGameCore, Warning, TEXT("[NullKeyStorage] BatchDelete called — no backend connected."));
    }

    virtual void ExecuteFunction(FGameplayTag, const FString& FunctionName,
        const FString&,
        TFunction<void(bool, const FString&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] ExecuteFunction '%s' called — no backend connected."),
            *FunctionName);
        Callback(false, TEXT("{}"));
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
- All `Get` / `BatchGet` callbacks must be assumed asynchronous. Never capture raw `UObject*` — use `TWeakObjectPtr`.
- `BatchGet` returns only found keys. Missing GUIDs are absent from the result map — implementations must not error on missing keys.
- `Delete` and `BatchDelete` are fire-and-forget. If confirmation is needed, use `ExecuteFunction` with a conditional delete script.
- The write-behind queue is keyed by `(StorageTag, Key)` — newer writes replace pending writes for the same key. This is the correct behavior for entity persistence: we always want the most recent state dispatched to the backend, never duplicates.
- `bCritical = true` entries are never dropped on queue overflow and are always flushed before normal entries. Use for logout, zone transfer, and server shutdown saves.
- `bFlushImmediately = true` bypasses the queue entirely and dispatches synchronously. Use sparingly — only when the caller cannot tolerate any flush delay (e.g. immediate logout on the hot path).
