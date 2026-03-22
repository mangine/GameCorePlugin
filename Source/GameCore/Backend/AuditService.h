#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Dom/JsonObject.h"
#include "AuditService.generated.h"

// ---------------------------------------------------------------------------
// FAuditEntry — Caller-provided fields only
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct GAMECORE_API FAuditEntry
{
    GENERATED_BODY()

    /** Structured event type — first-class indexed field for querying. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag EventTag;

    /** Payload schema version. Increment when the JSON layout changes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) uint8 SchemaVersion = 1;

    /** Who triggered the event. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid   ActorId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString ActorDisplayName; // For CS tooling only

    /** Optional: what the event acted on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid        SubjectId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag SubjectTag;

    /** Optional: play session correlation. Empty GUID is valid. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid SessionId;

    /** JSON payload built via FAuditPayloadBuilder. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Payload;
};

// ---------------------------------------------------------------------------
// FAuditEntryInternal — Used exclusively inside FAuditServiceBase
// ---------------------------------------------------------------------------

struct FAuditEntryInternal
{
    FAuditEntry Entry;

    FGuid     InstanceGUID;          // Cluster-unique event ID — stamped by FAuditServiceBase
    FString   ServerId;              // Stamped from SetServerId()
    FDateTime Timestamp;             // Enqueue time (UTC)

    uint64 TransactionGroupId = 0;
    bool   bTransactional     = false;
};

// ---------------------------------------------------------------------------
// IAuditService
// ---------------------------------------------------------------------------

UINTERFACE(MinimalAPI, NotBlueprintable)
class UAuditService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IAuditService
{
    GENERATED_BODY()

public:
    /** Called only by UGameCoreBackendSubsystem during Initialize. */
    virtual bool Connect(const FString& ConnectionString) = 0;

    /**
     * Flush all pending events — must complete before process exit.
     * Called automatically by UGameCoreBackendSubsystem::Deinitialize().
     */
    virtual void Flush() = 0;

    /**
     * Set the server identity. Until called, events are queued and not dispatched.
     * Once set, the pending queue is flushed and the flush timer starts.
     */
    virtual void SetServerId(const FString& InServerId) = 0;

    /** Enqueue a single event. Stamped internally with InstanceGUID, ServerId, Timestamp. */
    virtual void RecordEvent(const FAuditEntry& Entry) = 0;

    /**
     * Enqueue a group of events.
     * bTransactional = true: kept together and delivered as one atomic batch.
     */
    virtual void RecordBatch(TArray<FAuditEntry>&& Entries, bool bTransactional = false) = 0;

    virtual ~IAuditService() = default;
};

// ---------------------------------------------------------------------------
// FAuditServiceBase — Abstract base, owns write-behind queue
// ---------------------------------------------------------------------------

/**
 * Abstract C++ base class.
 * Owns queue, ServerId hold logic, flush logic, transactional group tracking.
 * Implementors override only DispatchBatch.
 *
 * DEVIATION: Uses manual flush (via Flush() call) rather than FTimerHandle,
 * since FAuditServiceBase is a plain C++ class without access to UWorld timer manager.
 */
class GAMECORE_API FAuditServiceBase : public IAuditService
{
public:
    float FlushIntervalSeconds  = 2.0f;
    int32 MaxQueueSize          = 10000;
    float FlushThresholdPercent = 0.75f;
    int32 MaxBatchSize          = 500;

    virtual bool Connect(const FString& ConnectionString) override;
    virtual void Flush() override;
    virtual void SetServerId(const FString& InServerId) override;
    virtual void RecordEvent(const FAuditEntry& Entry) override;
    virtual void RecordBatch(TArray<FAuditEntry>&& Entries, bool bTransactional = false) override;

protected:
    /**
     * Implementors override this only.
     * Receives a ready-to-send batch, already stamped.
     * bTransactional: batch must be committed atomically.
     */
    virtual void DispatchBatch(const TArray<FAuditEntryInternal>& Batch, bool bTransactional) = 0;

private:
    FString                     ServerId;
    bool                        bServerIdSet = false;
    TArray<FAuditEntryInternal> PendingQueue;
    uint64                      NextGroupId  = 1;
    bool                        bWarnedOnHalfFull = false;

    FAuditEntryInternal StampEntry(const FAuditEntry& Entry) const;
    void                FlushQueue();
    void                EnqueueInternal(FAuditEntryInternal&& Internal);
};

// ---------------------------------------------------------------------------
// FAuditPayloadBuilder — Type-safe JSON builder
// ---------------------------------------------------------------------------

struct GAMECORE_API FAuditPayloadBuilder
{
    FAuditPayloadBuilder& SetInt   (const FString& Key, int64 Value);
    FAuditPayloadBuilder& SetFloat (const FString& Key, float Value);
    FAuditPayloadBuilder& SetString(const FString& Key, const FString& Value);
    FAuditPayloadBuilder& SetBool  (const FString& Key, bool Value);
    FAuditPayloadBuilder& SetGuid  (const FString& Key, const FGuid& Value);
    FAuditPayloadBuilder& SetTag   (const FString& Key, const FGameplayTag& Value);

    /** Serializes to compact JSON string. */
    FString ToString() const;

private:
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
};

// ---------------------------------------------------------------------------
// FNullAuditService — Silent no-op
// ---------------------------------------------------------------------------

class GAMECORE_API FNullAuditService : public IAuditService
{
public:
    virtual bool Connect(const FString&) override { return true; }
    virtual void Flush() override {}
    virtual void SetServerId(const FString&) override {}
    virtual void RecordEvent(const FAuditEntry&) override {}
    virtual void RecordBatch(TArray<FAuditEntry>&&, bool) override {}
};
