# IAuditService

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Backend/AuditService.h`  

---

## Responsibility

Interface for recording immutable, auditable game events into an append-only backend. Used by any system that requires anti-cheat validation, exploit investigation, or rollback tooling — progression, market, inventory, trades, currency changes.

Accessed via `FGameCoreBackend::Audit()` (canonical) or `FGameCoreBackend::GetAudit(Tag)` for batch/transactional ops.

`InstanceGUID`, `ServerId`, and `Timestamp` are **stamped internally** by `FAuditServiceBase` — callers must never set them. `SessionId` is optional and caller-provided.

---

## FAuditEntry

Caller-provided fields only. Internal stamping happens inside `FAuditServiceBase`.

```cpp
USTRUCT()
struct GAMECORE_API FAuditEntry
{
    GENERATED_BODY()

    // Structured event type — first-class indexed field for querying.
    // Example: Audit.Progression.XPGain, Audit.Market.Trade, Audit.Inventory.ItemDrop
    FGameplayTag EventTag;

    // Payload schema version. Increment when the JSON layout changes.
    uint8 SchemaVersion = 1;

    // Who triggered the event.
    FGuid   ActorId;
    FString ActorDisplayName; // Human-readable — for CS tooling only, not machine parsing

    // Optional: what the event acted on (item, listing, quest, etc.)
    FGuid        SubjectId;
    FGameplayTag SubjectTag; // What kind of entity SubjectId refers to

    // Optional: play session correlation. Empty GUID is valid — omitted from record.
    FGuid SessionId;

    // JSON payload built via FAuditPayloadBuilder.
    FString Payload;
};
```

---

## FAuditEntryInternal

Used exclusively inside `FAuditServiceBase`. Never exposed to callers.

```cpp
struct FAuditEntryInternal
{
    FAuditEntry Entry;

    FGuid     InstanceGUID;         // Cluster-unique event ID — stamped by FAuditServiceBase
    FString   ServerId;             // Stamped from SetServerId()
    FDateTime Timestamp;            // Enqueue time (UTC) — stamped by FAuditServiceBase

    uint64 TransactionGroupId = 0;
    bool   bTransactional     = false;
};
```

---

## Interface Declaration

```cpp
UIINTERFACE(MinimalAPI, NotBlueprintable)
class UAuditService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IAuditService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during Initialize.
    virtual bool Connect(const FString& ConnectionString) = 0;

    // Flush all pending events — must complete before process exit.
    // Called automatically by UGameCoreBackendSubsystem::Deinitialize().
    virtual void Flush() = 0;

    // Set the server identity. Until called, events are queued and not dispatched.
    // Once set, the pending queue is flushed and the flush timer starts.
    virtual void SetServerId(const FString& InServerId) = 0;

    // Enqueue a single event. Stamped internally with InstanceGUID, ServerId, Timestamp.
    virtual void RecordEvent(const FAuditEntry& Entry) = 0;

    // Enqueue a group of events.
    // bTransactional = true: kept together and delivered as one atomic batch.
    virtual void RecordBatch(TArray<FAuditEntry>&& Entries, bool bTransactional = false) = 0;
};
```

---

## FAuditServiceBase

Abstract C++ base class. Owns the write-behind queue, ServerId hold logic, flush timer, and transactional group tracking. Implementors override only `DispatchBatch`.

```cpp
class GAMECORE_API FAuditServiceBase : public IAuditService
{
public:
    float FlushIntervalSeconds  = 2.0f;
    int32 MaxQueueSize          = 10000;  // Hard cap — oldest entries dropped on overflow
    float FlushThresholdPercent = 0.75f;  // Pressure flush at this fill ratio
    int32 MaxBatchSize          = 500;    // DispatchBatch splits into chunks of this size

    virtual bool Connect(const FString& ConnectionString) override;
    virtual void Flush() override;
    virtual void SetServerId(const FString& InServerId) override;
    virtual void RecordEvent(const FAuditEntry& Entry) override;
    virtual void RecordBatch(TArray<FAuditEntry>&& Entries, bool bTransactional = false) override;

protected:
    // Implementors override this only.
    // Receives a ready-to-send batch, already stamped.
    // bTransactional: batch must be committed atomically.
    // Implementors are responsible for retry logic and threading.
    virtual void DispatchBatch(const TArray<FAuditEntryInternal>& Batch, bool bTransactional) = 0;

private:
    FString                     ServerId;
    bool                        bServerIdSet = false;
    TArray<FAuditEntryInternal> PendingQueue;
    FTimerHandle                FlushTimerHandle;
    uint64                      NextGroupId = 1;

    FAuditEntryInternal StampEntry(const FAuditEntry& Entry) const;
    void                FlushQueue();
    void                EnqueueInternal(FAuditEntryInternal&& Internal);
};
```

### Queue Behavior

```
SetServerId() not yet called:
  RecordEvent / RecordBatch → PendingQueue (held, not dispatched)
  UE_LOG Warning fires once if queue exceeds 50% capacity while ServerId is unset

SetServerId() called:
  → All pending entries stamped with ServerId and flushed via DispatchBatch
  → FlushTimer started (FlushIntervalSeconds)
  → All subsequent calls → PendingQueue

FlushTimer fires (FlushIntervalSeconds):
  → PendingQueue drained → grouped by TransactionGroupId → DispatchBatch per group

Queue reaches FlushThresholdPercent * MaxQueueSize:
  → Immediate flush triggered (pressure flush) — bypasses timer

MaxQueueSize exceeded:
  → Oldest entries dropped, UE_LOG Warning emitted once per overflow event

Flush() — graceful shutdown:
  → Timer cleared → PendingQueue drained immediately
  → Called automatically by UGameCoreBackendSubsystem::Deinitialize()
```

---

## FAuditPayloadBuilder

Type-safe JSON builder for audit payloads. Eliminates raw `FString::Printf` at call sites.

```cpp
struct GAMECORE_API FAuditPayloadBuilder
{
    FAuditPayloadBuilder& SetInt   (const FString& Key, int64 Value);
    FAuditPayloadBuilder& SetFloat (const FString& Key, float Value);
    FAuditPayloadBuilder& SetString(const FString& Key, const FString& Value);
    FAuditPayloadBuilder& SetBool  (const FString& Key, bool Value);
    FAuditPayloadBuilder& SetGuid  (const FString& Key, const FGuid& Value);
    FAuditPayloadBuilder& SetTag   (const FString& Key, const FGameplayTag& Value);

    // Serializes to compact JSON string.
    FString ToString() const;

private:
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
};
```

**Usage:**
```cpp
Entry.Payload = FAuditPayloadBuilder{}
    .SetInt   (TEXT("price"),    Price)
    .SetString(TEXT("currency"), TEXT("Gold"))
    .SetGuid  (TEXT("listing"),  ListingId)
    .ToString();
```

---

## Null Fallback

`FNullAuditService` is a **silent no-op**. Audit is opt-in infrastructure — log spam when unconfigured has no value.

```cpp
class GAMECORE_API FNullAuditService : public IAuditService
{
public:
    virtual bool Connect(const FString&) override { return true; }
    virtual void Flush() override {}
    virtual void SetServerId(const FString&) override {}
    virtual void RecordEvent(const FAuditEntry&) override {}
    virtual void RecordBatch(TArray<FAuditEntry>&&, bool) override {}
};
```

To surface audit events in development without a real backend, bind `FGameCoreBackend::OnAudit` to a debug lambda.

---

## Recommended GameplayTag Namespaces

| Namespace | Usage |
|---|---|
| `Audit.Progression.*` | XP gains, level ups, skill changes |
| `Audit.Market.*` | Listings, trades, cancellations |
| `Audit.Inventory.*` | Item adds, removals, transfers |
| `Audit.Currency.*` | Gold gains, spends, transfers |
| `Audit.Admin.*` | GM/CS manual interventions |
| `Subject.Item.*` | Items acted upon |
| `Subject.Market.*` | Market listings acted upon |
| `Subject.Quest.*` | Quests acted upon |

---

## Notes

- `InstanceGUID`, `ServerId`, and `Timestamp` are always stamped internally — never set by callers.
- `SetServerId()` must be called before any events are dispatched. A `UE_LOG Warning` fires once if the queue hits 50% capacity while unset.
- `SessionId` is optional. Empty GUID is valid — field is omitted from the dispatched record.
- `bTransactional = true` guarantees atomic delivery only if the backend implementation supports it. Implementations that cannot honor transactionality must document this clearly.
- `Connect()` must only be called by `UGameCoreBackendSubsystem`.
- `Flush()` is called automatically by `UGameCoreBackendSubsystem::Deinitialize()`.
- `FAuditServiceBase` does not retry on `DispatchBatch` failure — implementors must implement retry internally.
- **Server-side only.**
