#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Backend/KeyStorageService.h"
#include "QueryStorageService.generated.h"

// ---------------------------------------------------------------------------
// Supporting Enums & Structs
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDBComparison : uint8
{
    Eq,
    NotEq,
    Lt,
    Lte,
    Gt,
    Gte,
    Contains,   // String or array contains
    StartsWith
};

UENUM(BlueprintType)
enum class EDBSortDirection : uint8
{
    Ascending,
    Descending
};

USTRUCT(BlueprintType)
struct GAMECORE_API FDBFieldPredicate
{
    GENERATED_BODY()

    /** Field name as declared in the backend schema */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName         FieldName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EDBComparison Op    = EDBComparison::Eq;
    /** String-encoded value. Backend implementation converts to native type. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString       Value;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FDBSortField
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName            FieldName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EDBSortDirection Direction = EDBSortDirection::Ascending;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FDBQueryFilter
{
    GENERATED_BODY()

    /** All predicates are AND-combined. OR logic must use ExecuteFunction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FDBFieldPredicate> Predicates;

    /** Optional sort. bHasSort must be true for Sort to be used. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool        bHasSort = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FDBSortField Sort;

    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Limit     = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Offset    = 0;      // Prefer PageToken for large datasets
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString PageToken;           // Implementation-specific cursor
};

USTRUCT(BlueprintType)
struct GAMECORE_API FDBQueryResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FGuid         RecordId;
    UPROPERTY(BlueprintReadOnly) TArray<uint8> Data;           // Caller deserializes according to SchemaTag
    UPROPERTY(BlueprintReadOnly) int64         UpdatedAt = 0;  // Unix timestamp (UTC)
};

// ---------------------------------------------------------------------------
// IQueryStorageService
// ---------------------------------------------------------------------------

UINTERFACE(MinimalAPI, NotBlueprintable)
class UQueryStorageService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IQueryStorageService
{
    GENERATED_BODY()

public:
    /** Called only by UGameCoreBackendSubsystem during Initialize. */
    virtual bool Connect(const FString& ConnectionString) = 0;

    // -------------------------------------------------------------------------
    // Single Record Ops
    // -------------------------------------------------------------------------

    /** Upsert a structured record. Data is the serialized record body. */
    virtual void Upsert(
        FGameplayTag            SchemaTag,
        const FGuid&            RecordId,
        const TArray<uint8>&    Data) = 0;

    /** Callback invoked on the game thread. */
    virtual void GetById(
        FGameplayTag    SchemaTag,
        const FGuid&    RecordId,
        TFunction<void(EStorageRequestResult Result, const FDBQueryResult& OutResult)> Callback) = 0;

    virtual void Delete(
        FGameplayTag    SchemaTag,
        const FGuid&    RecordId) = 0;

    // -------------------------------------------------------------------------
    // Query Ops
    // -------------------------------------------------------------------------

    /** Callbacks invoked on the game thread. */
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
    /** Maps to a stored procedure or registered handler on the backend. */
    virtual void ExecuteFunction(
        FGameplayTag            SchemaTag,
        const FString&          FunctionName,
        const FString&          ArgsJson,
        TFunction<void(EStorageRequestResult Result, const FString& ResultJson)> Callback) = 0;

    virtual ~IQueryStorageService() = default;
};

// ---------------------------------------------------------------------------
// FNullQueryStorageService
// ---------------------------------------------------------------------------

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
