#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KeyStorageService.generated.h"

// ---------------------------------------------------------------------------
// EStorageRequestResult
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EStorageRequestResult : uint8
{
    Success,    // Data returned successfully.
    Failure,    // Backend error or key not found.
    Cancelled,  // Cancel() was called before the backend responded.
    TimedOut,   // Implementation-defined timeout elapsed.
};

// ---------------------------------------------------------------------------
// FKeyStorageConfig
// ---------------------------------------------------------------------------

USTRUCT()
struct GAMECORE_API FKeyStorageConfig
{
    GENERATED_BODY()

    /** Backend endpoint — format is implementation-defined. */
    UPROPERTY() FString ConnectionString;

    /** Interval between periodic flushes of the write-behind queue (seconds). */
    UPROPERTY() float FlushIntervalSeconds = 5.0f;

    /** Immediate flush triggered when queue reaches this fraction of MaxQueueSize. */
    UPROPERTY() float FlushThresholdPercent = 0.75f;

    /** Hard cap on write-behind queue size (normal lane). Priority lane never dropped. */
    UPROPERTY() int32 MaxQueueSize = 5000;

    /** Maximum number of entries dispatched per backend call. */
    UPROPERTY() int32 MaxBatchSize = 200;

    /** Reconnect backoff. */
    UPROPERTY() float ReconnectDelaySeconds    = 2.0f;
    UPROPERTY() float MaxReconnectDelaySeconds = 60.0f;
};

// ---------------------------------------------------------------------------
// IKeyStorageService
// ---------------------------------------------------------------------------

UINTERFACE(MinimalAPI, NotBlueprintable)
class UKeyStorageService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IKeyStorageService
{
    GENERATED_BODY()

public:
    /** Called only by UGameCoreBackendSubsystem during Initialize. */
    virtual bool Connect(const FKeyStorageConfig& Config) = 0;

    // -------------------------------------------------------------------------
    // Single Record Ops
    // -------------------------------------------------------------------------

    /**
     * Enqueue a write into the write-behind queue.
     * bFlushImmediately: bypass queue, dispatch on service's internal thread.
     * bCritical: priority lane — never dropped on overflow.
     * The calling thread is NEVER blocked.
     */
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

    /**
     * Returns a request handle for Cancel().
     * Callback always fires — with Cancelled or TimedOut on failure.
     * Callbacks are invoked on the game thread.
     */
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

    /** bCritical applies to all entries in the batch. */
    virtual void BatchSet(
        FGameplayTag                               StorageTag,
        const TArray<TPair<FGuid, TArray<uint8>>>& Pairs,
        bool                                       bFlushImmediately = false,
        bool                                       bCritical         = false) = 0;

    /**
     * Missing keys are absent from the result map — not an error.
     * Callback always fires. Returns request handle for Cancel().
     */
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
    /** Args and result are JSON strings. Returns request handle for Cancel(). */
    virtual FGuid ExecuteFunction(
        FGameplayTag    StorageTag,
        const FString&  FunctionName,
        const FString&  ArgsJson,
        TFunction<void(EStorageRequestResult Result, const FString& ResultJson)> Callback) = 0;

    // -------------------------------------------------------------------------
    // Cancellation
    // -------------------------------------------------------------------------
    /** No-op if the request has already completed. */
    virtual void Cancel(FGuid RequestHandle) = 0;

    virtual ~IKeyStorageService() = default;
};

// ---------------------------------------------------------------------------
// FNullKeyStorageService
// ---------------------------------------------------------------------------

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
