# IQueryStorageService

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Backend/QueryStorageService.h`  

---

## Responsibility

Interface for structured, queryable storage backends (PostgreSQL, Elasticsearch, CockroachDB, etc.). Records are searchable by field values, sortable, and paginatable. GameCore only defines the filter and result contracts — the backend owns the schema.

Intended for data that needs to be **found without knowing its key**: market listings, leaderboards, guild registries, crafting recipes, admin lookups.

Accessed via `FGameCoreBackend::GetQueryStorage(Tag)`.

---

## Supporting Structs

```cpp
UENUM()
enum class EDBComparison : uint8
{
    Eq,
    NotEq,
    Lt,
    Lte,
    Gt,
    Gte,
    Contains,    // String or array contains
    StartsWith
};

UENUM()
enum class EDBSortDirection : uint8
{
    Ascending,
    Descending
};

USTRUCT()
struct GAMECORE_API FDBFieldPredicate
{
    GENERATED_BODY()

    // Field name as declared in the backend schema
    UPROPERTY() FName          FieldName;
    UPROPERTY() EDBComparison  Op    = EDBComparison::Eq;

    // String-encoded value. Backend implementation converts to native type.
    UPROPERTY() FString        Value;
};

USTRUCT()
struct GAMECORE_API FDBSortField
{
    GENERATED_BODY()

    UPROPERTY() FName              FieldName;
    UPROPERTY() EDBSortDirection   Direction = EDBSortDirection::Ascending;
};

USTRUCT()
struct GAMECORE_API FDBQueryFilter
{
    GENERATED_BODY()

    // All predicates are AND-combined. OR logic must use ExecuteFunction.
    UPROPERTY() TArray<FDBFieldPredicate>  Predicates;
    UPROPERTY() TOptional<FDBSortField>    Sort;

    UPROPERTY() int32   Limit     = 100;
    UPROPERTY() int32   Offset    = 0;       // Prefer PageToken for large datasets
    UPROPERTY() FString PageToken;           // Implementation-specific cursor; empty = first page
};

USTRUCT()
struct GAMECORE_API FDBQueryResult
{
    GENERATED_BODY()

    UPROPERTY() FGuid         RecordId;
    UPROPERTY() TArray<uint8> Data;        // Caller deserializes according to SchemaTag
    UPROPERTY() int64         UpdatedAt = 0; // Unix timestamp (UTC)
};
```

---

## Interface Declaration

```cpp
UIINTERFACE(MinimalAPI, NotBlueprintable)
class UQueryStorageService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IQueryStorageService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during Initialize.
    virtual bool Connect(const FString& ConnectionString) = 0;

    // -------------------------------------------------------------------------
    // Single Record Ops
    // -------------------------------------------------------------------------

    // Upsert a structured record. Data is the serialized record body.
    virtual void Upsert(
        FGameplayTag            SchemaTag,
        const FGuid&            RecordId,
        const TArray<uint8>&    Data) = 0;

    // Callback invoked on the game thread.
    virtual void GetById(
        FGameplayTag    SchemaTag,
        const FGuid&    RecordId,
        TFunction<void(EStorageRequestResult Result, const FDBQueryResult& Result)> Callback) = 0;

    virtual void Delete(
        FGameplayTag    SchemaTag,
        const FGuid&    RecordId) = 0;

    // -------------------------------------------------------------------------
    // Query Ops
    // -------------------------------------------------------------------------

    // Callbacks invoked on the game thread.
    virtual void Query(
        FGameplayTag            SchemaTag,
        const FDBQueryFilter&   Filter,
        TFunction<void(EStorageRequestResult Result, const TArray<FDBQueryResult>& Results, const FString& NextPageToken)> Callback) = 0;

    virtual void Count(
        FGameplayTag            SchemaTag,
        const FDBQueryFilter&   Filter,
        TFunction<void(EStorageRequestResult Result, int32 TotalCount)> Callback) = 0;

    // -------------------------------------------------------------------------
    // Batch Ops
    // -------------------------------------------------------------------------

    virtual void BatchUpsert(
        FGameplayTag                                    SchemaTag,
        const TArray<TPair<FGuid, TArray<uint8>>>&      Records) = 0;

    virtual void BatchDelete(
        FGameplayTag            SchemaTag,
        const TArray<FGuid>&    RecordIds) = 0;

    // -------------------------------------------------------------------------
    // Server-Side Function Execution
    // -------------------------------------------------------------------------
    // Maps to a stored procedure or registered handler on the backend.
    // Args and result are JSON strings.
    // Callback invoked on the game thread.
    virtual void ExecuteFunction(
        FGameplayTag            SchemaTag,
        const FString&          FunctionName,
        const FString&          ArgsJson,
        TFunction<void(EStorageRequestResult Result, const FString& ResultJson)> Callback) = 0;
};
```

> **Note:** Callbacks use `EStorageRequestResult` (aligned with `IKeyStorageService`) rather than `bool bSuccess`. This supersedes the original `bool bSuccess` signatures. See Architecture.md Known Issue #3.

---

## Null Fallback

```cpp
class GAMECORE_API FNullQueryStorageService : public IQueryStorageService
{
public:
    virtual bool Connect(const FString&) override { return true; }

    virtual void Upsert(FGameplayTag, const FGuid& RecordId, const TArray<uint8>&) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] Upsert called for record %s — no backend connected."), *RecordId.ToString());
    }

    virtual void GetById(FGameplayTag, const FGuid& RecordId,
        TFunction<void(EStorageRequestResult, const FDBQueryResult&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] GetById called for record %s — returning failure."), *RecordId.ToString());
        Callback(EStorageRequestResult::Failure, FDBQueryResult{});
    }

    virtual void Delete(FGameplayTag, const FGuid& RecordId) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] Delete called for record %s — no backend connected."), *RecordId.ToString());
    }

    virtual void Query(FGameplayTag, const FDBQueryFilter&,
        TFunction<void(EStorageRequestResult, const TArray<FDBQueryResult>&, const FString&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning, TEXT("[NullQueryStorage] Query called — returning empty results."));
        Callback(EStorageRequestResult::Failure, {}, TEXT(""));
    }

    virtual void Count(FGameplayTag, const FDBQueryFilter&,
        TFunction<void(EStorageRequestResult, int32)> Callback) override
    {
        UE_LOG(LogGameCore, Warning, TEXT("[NullQueryStorage] Count called — returning 0."));
        Callback(EStorageRequestResult::Failure, 0);
    }

    virtual void BatchUpsert(FGameplayTag, const TArray<TPair<FGuid, TArray<uint8>>>&) override
    {
        UE_LOG(LogGameCore, Warning, TEXT("[NullQueryStorage] BatchUpsert called — no backend connected."));
    }

    virtual void BatchDelete(FGameplayTag, const TArray<FGuid>&) override
    {
        UE_LOG(LogGameCore, Warning, TEXT("[NullQueryStorage] BatchDelete called — no backend connected."));
    }

    virtual void ExecuteFunction(FGameplayTag, const FString& FunctionName, const FString&,
        TFunction<void(EStorageRequestResult, const FString&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] ExecuteFunction '%s' called — no backend connected."), *FunctionName);
        Callback(EStorageRequestResult::Failure, TEXT("{}"));
    }
};
```

---

## ExecuteFunction Use Cases

| Use Case | Example |
|---|---|
| Atomic bid resolution | Outbid buyer + update listing price + write history row |
| Conditional price update | Only update if current price matches expected (optimistic lock) |
| Expiry sweep | Delete all listings where `ExpiresAt < now()` |

---

## Notes

- `SchemaTag` is the namespace/table discriminator — implementations route to the appropriate table, collection, or index.
- `FDBQueryFilter.Predicates` are AND-combined. OR logic must use `ExecuteFunction` or multiple merged queries.
- `PageToken` is preferred over `Offset` for large datasets — offset-based pagination degrades at scale on most backends.
- `Connect()` must only be called by `UGameCoreBackendSubsystem`.
- All callbacks must be treated as asynchronous. Never capture raw `UObject*` — use `TWeakObjectPtr`.
- `BatchDelete` is fire-and-forget. Use `ExecuteFunction` for transactional confirmation.
- This interface is **server-side only**.
