# IKeyStorageService

**Module:** `GameCore`

**Location:** `GameCore/Source/GameCore/Backend/KeyStorageService.h`

Interface for key-value blob storage backends. GameCore is agnostic to whether the backend is RAM-based (Redis), document-based (MongoDB), or any other store. The only contract is **key-tag-binary** storage with optional TTL and server-side function execution for atomic or complex operations.

Uses `FGameplayTag` as the namespace/collection discriminator and `FGuid` as the record key. Data is exchanged as binary (`FArchive` for single ops, `TArray<uint8>` for batch ops — references cannot be stored in collections).

> Replaces `IDBService`. Rename `DBService.h` → `KeyStorageService.h` and update `UGameCoreBackendSubsystem` accordingly.

---

## Interface Declaration

```cpp
UINTERFACE(MinimalAPI, NotBlueprintable)
class UKeyStorageService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IKeyStorageService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during registration
    virtual bool Connect(const FString& ConnectionString) = 0;

    // --- Single Record Ops ---
    virtual void Set(
        FGameplayTag    StorageTag,
        const FGuid&    Key,
        FArchive&       Data) = 0;

    virtual void SetWithTTL(
        FGameplayTag    StorageTag,
        const FGuid&    Key,
        FArchive&       Data,
        int32           TTLSeconds) = 0;

    virtual void Get(
        FGameplayTag    StorageTag,
        const FGuid&    Key,
        TFunction<void(bool bSuccess, FArchive& Data)> Callback) = 0;

    virtual void Delete(
        FGameplayTag    StorageTag,
        const FGuid&    Key) = 0;

    // --- Batch Ops ---
    // Payload is TArray<uint8> (not FArchive&) because references cannot be stored in collections.
    // Callers write to FMemoryWriter, then pass the byte array.
    virtual void BatchSet(
        FGameplayTag                                        StorageTag,
        const TArray<TPair<FGuid, TArray<uint8>>>&          Pairs) = 0;

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
    // Args and result are serialized as JSON strings — implementation is responsible
    // for type mapping.
    virtual void ExecuteFunction(
        FGameplayTag            StorageTag,
        const FString&          FunctionName,
        const FString&          ArgsJson,
        TFunction<void(bool bSuccess, const FString& ResultJson)> Callback) = 0;
};
```

---

## Null Fallback Implementation

Used automatically by `UGameCoreBackendSubsystem` when no service is registered or connection failed. Routes all operations to `UE_LOG` and returns failure/empty results safely.

```cpp
class GAMECORE_API FNullKeyStorageService : public IKeyStorageService
{
public:
    virtual bool Connect(const FString&) override { return true; }

    virtual void Set(FGameplayTag, const FGuid& Key, FArchive&) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Set called for key %s — no backend connected."),
            *Key.ToString());
    }

    virtual void SetWithTTL(FGameplayTag, const FGuid& Key, FArchive&, int32) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] SetWithTTL called for key %s — no backend connected."),
            *Key.ToString());
    }

    virtual void Get(FGameplayTag, const FGuid& Key,
        TFunction<void(bool, FArchive&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Get called for key %s — returning failure."),
            *Key.ToString());
        TArray<uint8> Empty;
        FMemoryReader DummyReader(Empty);
        Callback(false, DummyReader);
    }

    virtual void Delete(FGameplayTag, const FGuid& Key) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] Delete called for key %s — no backend connected."),
            *Key.ToString());
    }

    virtual void BatchSet(FGameplayTag,
        const TArray<TPair<FGuid, TArray<uint8>>>&) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] BatchSet called — no backend connected."));
    }

    virtual void BatchGet(FGameplayTag, const TArray<FGuid>&,
        TFunction<void(const TMap<FGuid, TArray<uint8>>&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] BatchGet called — returning empty results."));
        Callback({});
    }

    virtual void BatchDelete(FGameplayTag, const TArray<FGuid>&) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullKeyStorage] BatchDelete called — no backend connected."));
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

`UPersistenceSubsystem` dispatches `FEntityPersistencePayload` via tag delegates. To wire key storage, bind to those delegates and forward to `IKeyStorageService`.

`FEntityPersistencePayload` carries `PersistenceTag` and `EntityId`, which map directly to `IKeyStorageService::Set`'s `StorageTag` and `Key`. The `Components` array should be serialized into a single `FMemoryWriter` per entity.

```cpp
// Wiring lives in the game module — never in GameCore
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
            // ... serialize Payload.Components into Writer ...

            Backend->GetKeyStorage()->Set(
                Payload.PersistenceTag,
                Payload.EntityId,
                Writer);
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

`ExecuteFunction` maps to a backend-native callable — a Lua script in Redis, a stored procedure in a document store, or a custom handler in a middleware layer. It is **not** for game logic. Use it for:

- Atomic compare-and-swap on inventory slots
- Conditional expiry resets
- Aggregated counters that must not race

Args and result use JSON strings. The implementation is responsible for type mapping. GameCore imposes no schema on the payload.

---

## Notes

- `StorageTag` serves as namespace/collection discriminator. Implementations may route to different collections, key prefixes, or Redis databases.
- `Connect()` is public for interface technical reasons but **must only be called by `UGameCoreBackendSubsystem`**.
- All callbacks must be assumed asynchronous. Never capture raw `UObject*` — use `TWeakObjectPtr`.
- `BatchGet` returns only found keys. Missing GUIDs are absent from the result map — implementations must not error on missing keys.
- `Delete` and `BatchDelete` are fire-and-forget. If confirmation is needed, use `ExecuteFunction` with a conditional delete script.
