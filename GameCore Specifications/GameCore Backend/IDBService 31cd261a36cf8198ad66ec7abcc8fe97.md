# IDBService

**Module:** `GameCore`

**Location:** `GameCore/Source/GameCore/Backend/DBService.h`

Interface for external database services. GameCore is agnostic to whether the backend is RAM-based (Redis), relational (Postgres), or document-based. The only contract is key-tag-binary storage.

Uses `FGameplayTag` as the data type discriminator (matching `FEntityPersistencePayload.PersistenceTag`) and `FGuid` as the record key. Data is exchanged as binary (`FArchive` for single ops, `TArray<uint8>` for batch ops since references cannot be stored in collections).

---

## Interface Declaration

```cpp
UINTERFACE(MinimalAPI, NotBlueprintable)
class UDBService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IDBService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during registration
    virtual bool Connect(const FString& ConnectionString) = 0;

    // --- Single Record Ops ---
    virtual void Set(
        FGameplayTag    PersistenceTag,
        const FGuid&    Key,
        FArchive&       Data) = 0;

    virtual void Get(
        FGameplayTag    PersistenceTag,
        const FGuid&    Key,
        TFunction<void(bool bSuccess, FArchive& Data)> Callback) = 0;

    // --- Batch Ops ---
    // Payload is TArray<uint8> (not FArchive&) because references cannot be stored in collections.
    // Callers write to FMemoryWriter, then pass the byte array.
    virtual void BatchSet(
        FGameplayTag                              PersistenceTag,
        const TArray<TPair<FGuid, TArray<uint8>>>& Pairs) = 0;

    virtual void BatchGet(
        FGameplayTag              PersistenceTag,
        const TArray<FGuid>&      Keys,
        TFunction<void(const TMap<FGuid, TArray<uint8>>& Results)> Callback) = 0;
};
```

---

## Null Fallback Implementation

Used automatically by `UGameCoreBackendSubsystem` when no service is registered or connection failed. Routes all operations to `UE_LOG` and returns failure.

```cpp
class GAMECORE_API FNullDBService : public IDBService
{
public:
    virtual bool Connect(const FString&) override { return true; }

    virtual void Set(FGameplayTag, const FGuid& Key, FArchive&) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullDB] Set called for key %s — no backend connected."),
            *Key.ToString());
    }

    virtual void Get(FGameplayTag, const FGuid& Key,
        TFunction<void(bool, FArchive&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullDB] Get called for key %s — returning failure."),
            *Key.ToString());
        TArray<uint8> Empty;
        FMemoryReader DummyReader(Empty);
        Callback(false, DummyReader);
    }

    virtual void BatchSet(FGameplayTag,
        const TArray<TPair<FGuid, TArray<uint8>>>&) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullDB] BatchSet called — no backend connected."));
    }

    virtual void BatchGet(FGameplayTag, const TArray<FGuid>&,
        TFunction<void(const TMap<FGuid, TArray<uint8>>&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullDB] BatchGet called — returning empty results."));
        Callback({});
    }
};
```

---

## Wiring with UPersistenceSubsystem

`UPersistenceSubsystem` dispatches `FEntityPersistencePayload` via tag delegates. To wire DB storage, bind to those delegates and forward to `IDBService`.

`FEntityPersistencePayload` already carries `PersistenceTag` and `EntityId`, which map directly to `IDBService::Set`'s `PersistenceTag` and `Key` parameters. The `Components` array should be serialized into a single `FMemoryWriter` per entity.

```cpp
// Example wiring in game module startup (NOT in GameCore)
void UMyGameServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UPersistenceSubsystem>();
    Collection.InitializeDependency<UGameCoreBackendSubsystem>();

    auto* Persistence = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>();
    auto* Backend     = GetGameInstance()->GetSubsystem<UGameCoreBackendSubsystem>();

    // Bind the player persistence tag to DB writes
    Persistence->RegisterPersistenceTag(TAG_Persistence_Entity_Player);
    Persistence->GetSaveDelegate(TAG_Persistence_Entity_Player)
        ->AddLambda([Backend](const FEntityPersistencePayload& Payload)
        {
            // Serialize all component blobs into a single byte array
            TArray<uint8> Bytes;
            FMemoryWriter Writer(Bytes);
            // ... serialize Payload.Components into Writer ...

            Backend->GetDB()->Set(
                Payload.PersistenceTag,
                Payload.EntityId,
                Writer);
        });
}
```

> This wiring lives in the **game module**, never in GameCore. GameCore systems are intentionally decoupled.
> 

---

## Notes

- `PersistenceTag` serves as a namespace/table discriminator — implementations may use it to route to different collections, tables, or key prefixes.
- `Connect()` is public for interface technical reasons but **must only be called by `UGameCoreBackendSubsystem`**.
- All callbacks must be assumed to fire asynchronously. Never capture raw `UObject*` in callbacks without a weak pointer guard.
- `BatchGet` returns only found keys. Missing GUIDs are simply absent from the result map — implementations must not error on missing keys.