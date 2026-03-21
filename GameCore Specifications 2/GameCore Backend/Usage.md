# GameCore Backend — Usage Guide

---

## 1. Wiring in the Game Module

All service registration and tag routing happens once in `UGameInstance::Init`, before `Super::Init()`. The subsystem connects services and registers the facade during `Super::Init()`.

```cpp
// MyGameInstance.cpp
#include "Backend/BackendSubsystem.h"

void UMyGameInstance::Init()
{
    auto* Backend = GetSubsystem<UGameCoreBackendSubsystem>();

    // --- Register services ---
    Backend->RegisterKeyStorageService(TEXT("PlayerDB"),   PlayerRedisService,  PlayerDbConfig);
    Backend->RegisterKeyStorageService(TEXT("EconomyDB"),  EconomyRedisService, EconomyDbConfig);
    Backend->RegisterQueryStorageService(TEXT("EconomyDB"), EconomyPostgresService, TEXT("postgres://..."));
    Backend->RegisterAuditService(TEXT("Security"),  SecurityAuditService,  TEXT("postgres://audit-host/security"));
    Backend->RegisterAuditService(TEXT("Gameplay"),  GameplayAuditService,  TEXT("postgres://audit-host/gameplay"));
    Backend->RegisterLoggingService(NAME_None,           DatadogLogger, DefaultLogConfig);
    Backend->RegisterLoggingService(TEXT("SecurityLog"),  SecurityLogger, SecurityLogConfig);

    // --- Map tags to named instances ---
    // Key storage
    Backend->MapTagToKeyStorage(TAG_Persistence_Entity_Player,   TEXT("PlayerDB"));
    Backend->MapTagToKeyStorage(TAG_Persistence_Entity_Quest,    TEXT("PlayerDB"));
    Backend->MapTagToKeyStorage(TAG_Persistence_Economy_Listing, TEXT("EconomyDB"));

    // Query storage
    Backend->MapTagToQueryStorage(TAG_Schema_Market_Listing, TEXT("EconomyDB"));
    Backend->MapTagToQueryStorage(TAG_Schema_Leaderboard,    TEXT("EconomyDB"));

    // Audit
    Backend->MapTagToAudit(TAG_Audit_Progression, TEXT("Gameplay"));
    Backend->MapTagToAudit(TAG_Audit_Market,      TEXT("Gameplay"));
    Backend->MapTagToAudit(TAG_Audit_AntiCheat,   TEXT("Security"));

    // Logging (unmapped tags fall back to NAME_None default logger)
    Backend->MapTagToLogging(TAG_Log_Security, TEXT("SecurityLog"));

    Super::Init(); // ← subsystem Initialize() runs here, Connect() is called
}
```

---

## 2. Logging

Plugin systems include only `GameCoreBackend.h`. No context object needed.

```cpp
#include "Backend/GameCoreBackend.h"

// Preferred: canonical call — routes through OnLog delegate if bound
FGameCoreBackend::Log(ELogSeverity::Warning, TEXT("Persistence"), TEXT("Flush stalled"));
FGameCoreBackend::Log(ELogSeverity::Error,   TEXT("Market"),      TEXT("Listing expired"), PayloadJson);

// Direct service access (needed for LogInfo/LogWarning convenience methods)
FGameCoreBackend::GetLogging(TAG_Log_Security)->LogCritical(TEXT("AntiCheat"), Details);
```

Logging **always** reaches `UE_LOG` at minimum — the null fallback is never silent.

---

## 3. Audit

```cpp
#include "Backend/GameCoreBackend.h"
#include "Backend/AuditService.h"

FAuditEntry Entry;
Entry.EventTag         = TAG_Audit_Market_Trade;
Entry.SchemaVersion    = 1;
Entry.ActorId          = BuyerActorId;
Entry.ActorDisplayName = BuyerName;
Entry.SubjectId        = ListingId;
Entry.SubjectTag       = TAG_Subject_Market_Listing;
Entry.SessionId        = SessionId;
Entry.Payload          = FAuditPayloadBuilder{}
    .SetInt   (TEXT("price"),    Price)
    .SetString(TEXT("currency"), TEXT("Gold"))
    .SetGuid  (TEXT("listing"),  ListingId)
    .ToString();

// Canonical call — routes through OnAudit delegate if bound
FGameCoreBackend::Audit(Entry);

// Batch with transactional guarantee (e.g. trade debit + credit must be atomic)
TArray<FAuditEntry> Entries;
Entries.Add(DebitEntry);
Entries.Add(CreditEntry);
FGameCoreBackend::GetAudit(TAG_Audit_Market)->RecordBatch(MoveTemp(Entries), /*bTransactional=*/true);
```

Do **not** set `InstanceGUID`, `ServerId`, or `Timestamp` — these are stamped internally by `FAuditServiceBase`.

---

## 4. Key-Value Storage

### Write (fire-and-forget)
```cpp
TArray<uint8> Bytes;
FMemoryWriter Writer(Bytes);
// ... serialize entity ...

// Normal write — goes into write-behind queue
FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)->Set(
    TAG_Persistence_Entity_Player, EntityId, Bytes);

// Critical write (logout, zone transfer) — priority lane, never dropped on overflow
// bFlushImmediately dispatches on service's internal thread — game thread never blocked
FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)->Set(
    TAG_Persistence_Entity_Player, EntityId, Bytes,
    /*bFlushImmediately=*/true, /*bCritical=*/true);
```

### Read with cancellation
```cpp
FGuid LoadHandle = FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)->Get(
    TAG_Persistence_Entity_Player, PlayerId,
    [this, WeakSelf = TWeakObjectPtr<UMySystem>(this)]
    (EStorageRequestResult Result, const TArray<uint8>& Data)
    {
        if (!WeakSelf.IsValid()) return;

        if (Result == EStorageRequestResult::Success)
        {
            FMemoryReader Reader(Data);
            // ... deserialize ...
        }
        else
        {
            // Failure, Cancelled, or TimedOut — all are handled the same way
            UE_LOG(LogGame, Error, TEXT("Load failed: %s"), *UEnum::GetValueAsString(Result));
        }
    });

// Cancel if the requesting object is destroyed before the backend responds
FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)->Cancel(LoadHandle);
```

### TTL write (ephemeral data)
```cpp
// Session lock — expires after 30 s if server crashes without explicit Delete
FGameCoreBackend::GetKeyStorage(TAG_Session_Lock)->SetWithTTL(
    TAG_Session_Lock, SessionId, LockBytes, /*TTLSeconds=*/30);
```

### Batch write
```cpp
TArray<TPair<FGuid, TArray<uint8>>> Pairs;
for (const auto& Entity : DirtyEntities)
    Pairs.Emplace(Entity.Id, Entity.SerializedBytes);

FGameCoreBackend::GetKeyStorage(TAG_Persistence_Entity_Player)->BatchSet(
    TAG_Persistence_Entity_Player, Pairs, /*bFlushImmediately=*/false, /*bCritical=*/false);
```

### Server-side atomic operation
```cpp
// Atomic inventory slot compare-and-swap via a Lua script registered on the Redis backend
FString ArgsJson = FString::Printf(TEXT("{\"slot\":%d,\"expectedItemId\":\"%s\"}"),
    SlotIndex, *ExpectedItemId.ToString());

FGuid OpHandle = FGameCoreBackend::GetKeyStorage(TAG_Persistence_Inventory)->ExecuteFunction(
    TAG_Persistence_Inventory, TEXT("swap_inventory_slot"), ArgsJson,
    [](EStorageRequestResult Result, const FString& ResultJson)
    {
        if (Result == EStorageRequestResult::Success)
            // parse ResultJson ...
        else
            // handle failure
        ;
    });
```

---

## 5. Query Storage

```cpp
// Write (upsert)
FGameCoreBackend::GetQueryStorage(TAG_Schema_Market_Listing)->Upsert(
    TAG_Schema_Market_Listing, Listing.ListingId, SerializedBytes);

// Filtered query with pagination
FDBQueryFilter Filter;
Filter.Predicates.Add({ TEXT("ItemType"), EDBComparison::Eq, ItemTypeName });
Filter.Sort  = FDBSortField{ TEXT("Price"), EDBSortDirection::Ascending };
Filter.Limit = 50;

FGameCoreBackend::GetQueryStorage(TAG_Schema_Market_Listing)->Query(
    TAG_Schema_Market_Listing, Filter,
    [](bool bSuccess, const TArray<FDBQueryResult>& Results, const FString& NextPageToken)
    {
        if (!bSuccess) return;
        for (const FDBQueryResult& Row : Results)
        {
            FMemoryReader Reader(Row.Data);
            // ... deserialize ...
        }
        // Use NextPageToken for the next page if non-empty
    });
```

---

## 6. Lightweight Delegate Wiring (no subsystem)

For projects that don't need the full subsystem stack, bind delegates directly in `GameInstance::Init`.

```cpp
// Log only — no subsystem
FGameCoreBackend::OnLog = [](ELogSeverity Severity, const FString& Category,
                              const FString& Message, const FString& Payload)
{
    MyExternalLogger::Send(Category, Message, static_cast<int>(Severity));
};

// Audit only — forward to an analytics SDK
FGameCoreBackend::OnAudit = [](const FAuditEntry& Entry)
{
    MyAnalyticsSDK::Track(Entry.EventTag.ToString(), Entry.Payload);
};

// Persistence write — forward to a custom DB adapter
FGameCoreBackend::OnPersistenceWrite =
    [Adapter](FGameplayTag Tag, FGuid EntityId, TArrayView<const uint8> Bytes)
{
    Adapter->Upsert(Tag.GetTagName().ToString(), EntityId, Bytes);
};

// Always clear on shutdown to avoid dangling captures
void UMyGameInstance::Shutdown()
{
    FGameCoreBackend::OnLog             = nullptr;
    FGameCoreBackend::OnAudit           = nullptr;
    FGameCoreBackend::OnPersistenceWrite = nullptr;
    Super::Shutdown();
}
```

> **Rule:** Bind in `GameInstance::Init`. Clear in `GameInstance::Shutdown`. Never rebind mid-session.

---

## 7. Dev-Build Audit Visibility

The null audit service is silent by design. In dev builds, surface audit events without wiring a real backend:

```cpp
#if !UE_BUILD_SHIPPING
FGameCoreBackend::OnAudit = [](const FAuditEntry& Entry)
{
    UE_LOG(LogGameCore, Log, TEXT("[DEV Audit] %s | Actor=%s | %s"),
        *Entry.EventTag.ToString(), *Entry.ActorId.ToString(), *Entry.Payload);
};
#endif
```

---

## 8. Implementing a Custom Backend Service

### Logging backend

```cpp
// Extend FLoggingServiceBase — override only ConnectToBackend and DispatchBatch
class FDatadogLoggingService : public FLoggingServiceBase
{
protected:
    virtual bool ConnectToBackend(const FLoggingConfig& Config) override
    {
        // Open HTTP connection to Config.Endpoint
        return true; // or false on failure — base class will retry with backoff
    }

    virtual bool DispatchBatch(const TArray<FLogEntry>& Entries) override
    {
        // Serialize Entries to JSON, POST to Datadog Logs API
        // Respect Config.MaxBatchSize — already guaranteed by base class caller
        return true; // or false on failure
    }
};
```

### Audit backend

```cpp
// Extend FAuditServiceBase — override only DispatchBatch
class FPostgresAuditService : public FAuditServiceBase
{
protected:
    virtual void DispatchBatch(const TArray<FAuditEntryInternal>& Batch, bool bTransactional) override
    {
        // Build and execute a parameterized INSERT (with optional transaction wrapping)
        // Implement retry internally — FAuditServiceBase does not retry for you
    }
};
```

### Key storage backend

```cpp
// Implement IKeyStorageService directly
class FRedisKeyStorageService : public IKeyStorageService
{
public:
    virtual bool Connect(const FKeyStorageConfig& Config) override { /* ... */ return true; }
    virtual void Set(FGameplayTag Tag, const FGuid& Key, const TArray<uint8>& Data,
        bool bFlushImmediately, bool bCritical) override { /* enqueue into write-behind queue */ }
    // ... implement all pure virtuals ...
    virtual void Cancel(FGuid RequestHandle) override { /* suppress pending callback */ }
};
```
