# IQueryStorageService

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Backend/QueryStorageService.h`

Interface for structured, queryable storage backends (e.g. PostgreSQL, Elasticsearch, CockroachDB). Unlike `IKeyStorageService`, records here are searchable by field values, sortable, and paginatable. The backend owns the schema — GameCore only defines the filter and result contracts.

Intended for data that needs to be **found without knowing its key**: market listings, leaderboards, guild registries, crafting recipes, admin lookups, and similar structured datasets.

Accessed via `FGameCoreBackend::GetQueryStorage(Key)`.

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
    Contains,   // String/array contains
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

    // All predicates are AND-combined. OR logic must be expressed via ExecuteFunction.
    UPROPERTY() TArray<FDBFieldPredicate>   Predicates;
    UPROPERTY() TOptional<FDBSortField>     Sort;

    UPROPERTY() int32 Limit  = 100;
    UPROPERTY() int32 Offset = 0;   // For cursor-based pagination, prefer PageToken
    UPROPERTY() FString PageToken;  // Implementation-specific cursor; empty = first page
};

USTRUCT()
struct GAMECORE_API FDBQueryResult
{
    GENERATED_BODY()

    UPROPERTY() FGuid          RecordId;

    // Serialized record data. Caller deserializes according to SchemaTag.
    UPROPERTY() TArray<uint8>  Data;

    UPROPERTY() int64          UpdatedAt = 0; // Unix timestamp (UTC)
};
```

---

## Interface Declaration

```cpp
UINTERFACE(MinimalAPI, NotBlueprintable)
class UQueryStorageService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IQueryStorageService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during registration
    virtual bool Connect(const FString& ConnectionString) = 0;

    // --- Single Record Ops ---
    // Upsert a structured record. Data is the serialized record body.
    virtual void Upsert(
        FGameplayTag            SchemaTag,
        const FGuid&            RecordId,
        const TArray<uint8>&    Data) = 0;

    virtual void GetById(
        FGameplayTag    SchemaTag,
        const FGuid&    RecordId,
        TFunction<void(bool bSuccess, const FDBQueryResult& Result)> Callback) = 0;

    virtual void Delete(
        FGameplayTag    SchemaTag,
        const FGuid&    RecordId) = 0;

    // --- Query Ops ---
    virtual void Query(
        FGameplayTag            SchemaTag,
        const FDBQueryFilter&   Filter,
        TFunction<void(bool bSuccess, const TArray<FDBQueryResult>& Results, const FString& NextPageToken)> Callback) = 0;

    virtual void Count(
        FGameplayTag            SchemaTag,
        const FDBQueryFilter&   Filter,
        TFunction<void(bool bSuccess, int32 TotalCount)> Callback) = 0;

    // --- Batch Ops ---
    virtual void BatchUpsert(
        FGameplayTag                                        SchemaTag,
        const TArray<TPair<FGuid, TArray<uint8>>>&          Records) = 0;

    virtual void BatchDelete(
        FGameplayTag            SchemaTag,
        const TArray<FGuid>&    RecordIds) = 0;

    // --- Server-Side Function Execution ---
    // For atomic or complex operations not suitable for game-level logic:
    // e.g. conditional price updates, atomic bid resolution, expiry sweeps.
    // FunctionName maps to a stored procedure or registered handler on the backend.
    // Args and result are JSON strings — implementation is responsible for type mapping.
    virtual void ExecuteFunction(
        FGameplayTag            SchemaTag,
        const FString&          FunctionName,
        const FString&          ArgsJson,
        TFunction<void(bool bSuccess, const FString& ResultJson)> Callback) = 0;
};
```

---

## Null Fallback Implementation

`FNullQueryStorageService` is used by `FGameCoreBackend` when no service is registered or the subsystem is not live. Every call logs a `UE_LOG Warning` and invokes any callback with a failure result so callers are never silently stuck.

```cpp
class GAMECORE_API FNullQueryStorageService : public IQueryStorageService
{
public:
    virtual bool Connect(const FString&) override { return true; }

    virtual void Upsert(FGameplayTag, const FGuid& RecordId,
        const TArray<uint8>&) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] Upsert called for record %s — no backend connected."),
            *RecordId.ToString());
    }

    virtual void GetById(FGameplayTag, const FGuid& RecordId,
        TFunction<void(bool, const FDBQueryResult&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] GetById called for record %s — returning failure."),
            *RecordId.ToString());
        Callback(false, FDBQueryResult{});
    }

    virtual void Delete(FGameplayTag, const FGuid& RecordId) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] Delete called for record %s — no backend connected."),
            *RecordId.ToString());
    }

    virtual void Query(FGameplayTag, const FDBQueryFilter&,
        TFunction<void(bool, const TArray<FDBQueryResult>&, const FString&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] Query called — returning empty results."));
        Callback(false, {}, TEXT(""));
    }

    virtual void Count(FGameplayTag, const FDBQueryFilter&,
        TFunction<void(bool, int32)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] Count called — returning 0."));
        Callback(false, 0);
    }

    virtual void BatchUpsert(FGameplayTag,
        const TArray<TPair<FGuid, TArray<uint8>>>&) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] BatchUpsert called — no backend connected."));
    }

    virtual void BatchDelete(FGameplayTag, const TArray<FGuid>&) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] BatchDelete called — no backend connected."));
    }

    virtual void ExecuteFunction(FGameplayTag, const FString& FunctionName,
        const FString&,
        TFunction<void(bool, const FString&)> Callback) override
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[NullQueryStorage] ExecuteFunction '%s' called — no backend connected."),
            *FunctionName);
        Callback(false, TEXT("{}"));
    }
};
```

This implementation is declared in `QueryStorageService.h` and also instantiated as the static fallback `GNullQueryStorage` in `GameCoreBackend.cpp`.

---

## Usage Example — Market Listings

```cpp
// Posting a market listing (game module)
void UMarketSystem::PostListing(const FMarketListing& Listing)
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    // ... serialize Listing into Writer ...

    FGameCoreBackend::GetQueryStorage()->Upsert(
        TAG_Schema_Market_Listing,
        Listing.ListingId,
        Bytes);
}

// Searching listings (game module)
void UMarketSystem::SearchByItemType(FName ItemType,
    TFunction<void(const TArray<FMarketListing>&)> OnResults)
{
    FDBQueryFilter Filter;
    Filter.Predicates.Add({ TEXT("ItemType"), EDBComparison::Eq, ItemType.ToString() });
    Filter.Sort = FDBSortField{ TEXT("Price"), EDBSortDirection::Ascending };
    Filter.Limit = 50;

    FGameCoreBackend::GetQueryStorage()->Query(TAG_Schema_Market_Listing, Filter,
        [OnResults](bool bSuccess, const TArray<FDBQueryResult>& Results, const FString&)
        {
            if (!bSuccess) return;
            TArray<FMarketListing> Listings;
            for (const FDBQueryResult& Row : Results)
            {
                FMemoryReader Reader(Row.Data);
                FMarketListing Entry;
                // ... deserialize Entry from Reader ...
                Listings.Add(MoveTemp(Entry));
            }
            OnResults(Listings);
        });
}
```

---

## ExecuteFunction Contract

Use for operations that must not be split across network round trips:

| Use Case | Example |
|---|---|
| Atomic bid resolution | Outbid existing buyer, update listing price, write history row |
| Conditional price update | Only update if current price matches expected (optimistic lock) |
| Expiry sweep | Delete all listings where `ExpiresAt < now()` |

Args and result use JSON strings. Backend implementations are responsible for mapping to native types. GameCore imposes no schema on the payload.

---

## Notes

- `SchemaTag` is the namespace/table discriminator. Implementations route to the appropriate table, collection, or index.
- `FDBQueryFilter.Predicates` are AND-combined. OR logic must be expressed via `ExecuteFunction` or multiple `Query` calls merged client-side.
- `PageToken` is preferred over `Offset` for large datasets — `Offset`-based pagination degrades at scale on most backends.
- `Connect()` is public for interface technical reasons but **must only be called by `UGameCoreBackendSubsystem`**.
- All callbacks must be assumed asynchronous. Never capture raw `UObject*` — use `TWeakObjectPtr`.
- `BatchDelete` is fire-and-forget. Use `ExecuteFunction` if transactional confirmation is required.
