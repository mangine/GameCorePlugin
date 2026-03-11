# IAuditService

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Backend/AuditService.h`

Interface for recording immutable, auditable game events into an append-only backend. Used by any system that requires anti-cheat validation, exploit investigation, or rollback tooling — progression, market, inventory, trades, currency changes, etc.

`InstanceGUID`, `ServerId`, and `Timestamp` are **stamped internally** by `FAuditServiceBase` before dispatch — callers never provide them. `SessionId` is optional and caller-provided.

---

## FAuditEntry

Caller-provided fields only. Internal stamping happens inside `FAuditServiceBase::RecordEvent`.

```cpp
USTRUCT()
struct GAMECORE_API FAuditEntry
{
    GENERATED_BODY()

    // Structured event type — used as a first-class indexed field for querying.
    // Example: Audit.Progression.XPGain, Audit.Market.Trade, Audit.Inventory.ItemDrop
    FGameplayTag EventTag;

    // Payload schema version. Increment when the JSON layout changes.
    // Enables safe parsing of historical records after schema evolution.
    uint8 SchemaVersion = 1;

    // Who triggered the event. Must be a valid entity GUID.
    FGuid ActorId;

    // Human-readable actor name for CS tooling only. Not for machine parsing.
    FString ActorDisplayName;

    // Optional: what the event acted on (item, listing, quest, etc.)
    FGuid SubjectId;

    // Optional: what kind of entity SubjectId refers to.
    // Example: Subject.Item.Weapon, Subject.Market.Listing
    FGameplayTag SubjectTag;

    // Optional: play session correlation. Provided by the caller (e.g. from a session tracker).
    // Empty GUID if not available — omitted from the stamped record, not an error.
    FGuid SessionId;

    // JSON payload built via FAuditPayloadBuilder.
    // Schema is game-defined and versioned by SchemaVersion.
    FString Payload;
};
```

---

## FAuditEntryInternal

Used exclusively inside `FAuditServiceBase`. Wraps `FAuditEntry` with stamped fields and transactional group metadata.

```cpp
struct FAuditEntryInternal
{
    FAuditEntry Entry;

    // Stamped by FAuditServiceBase — cluster-unique event ID for deduplication.
    FGuid     InstanceGUID;

    // Stamped by FAuditServiceBase from SetServerId().
    FString   ServerId;

    // Stamped by FAuditServiceBase at enqueue time (UTC).
    FDateTime Timestamp;

    // Transactional group support — entries with the same non-zero GroupId
    // are kept together and flushed as a single transactional batch.
    uint64 TransactionGroupId = 0;
    bool   bTransactional     = false;
};
```

---

## Interface Declaration

```cpp
UINTERFACE(MinimalAPI, NotBlueprintable)
class UAuditService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IAuditService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during registration.
    virtual bool Connect(const FString& ConnectionString) = 0;

    // Flush all pending events immediately — must be called on graceful shutdown.
    // Called automatically by UGameCoreBackendSubsystem::Deinitialize().
    virtual void Flush() = 0;

    // Set the server identity. Until this is called, events are queued and not dispatched.
    // Once set, the pending queue is flushed and the flush timer starts.
    // All queued entries are stamped with this ServerId before dispatch.
    virtual void SetServerId(const FString& InServerId) = 0;

    // Enqueue a single event. Stamped internally with InstanceGUID, ServerId, Timestamp.
    virtual void RecordEvent(const FAuditEntry& Entry) = 0;

    // Enqueue a group of events.
    // bTransactional = true: entries are kept together and delivered as one atomic batch.
    // bTransactional = false (default): best-effort per entry on the backend.
    virtual void RecordBatch(const TArray<FAuditEntry>& Entries, bool bTransactional = false) = 0;
};
```

---

## FAuditServiceBase

Abstract C++ base class that all concrete audit backends extend. Owns the write-behind queue, ServerId hold logic, flush timer, and transactional group tracking. Implementors only override `DispatchBatch`.

```cpp
class GAMECORE_API FAuditServiceBase : public IAuditService
{
public:
    // --- Configuration (set before Connect) ---
    float FlushIntervalSeconds  = 2.0f;
    int32 MaxQueueSize          = 10000;  // Hard cap — oldest entries dropped on overflow
    float FlushThresholdPercent = 0.75f;  // Pressure flush: immediate flush when queue reaches this fill ratio
    int32 MaxBatchSize          = 500;    // Implementation hint: DispatchBatch must be split into chunks of this size

    // --- IAuditService ---
    virtual bool Connect(const FString& ConnectionString) override;
    virtual void Flush() override;
    virtual void SetServerId(const FString& InServerId) override;
    virtual void RecordEvent(const FAuditEntry& Entry) override;
    virtual void RecordBatch(const TArray<FAuditEntry>& Entries, bool bTransactional = false) override;

protected:
    // Implementors override this — receives a ready-to-send batch, already stamped.
    // bTransactional indicates whether the batch must be committed atomically.
    //
    // Implementation requirements:
    //   - Respect MaxBatchSize: split large pending queues into multiple DispatchBatch calls.
    //   - Implement retry logic internally — FAuditServiceBase does not retry on failure.
    //   - Threading is the implementor's responsibility.
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
SetServerId() not called yet:
  RecordEvent / RecordBatch → PendingQueue (held, not dispatched)
  UE_LOG Warning fires once if queue reaches 50% capacity while ServerId is unset

SetServerId() called:
  → All pending entries stamped with ServerId and flushed via DispatchBatch
  → FlushTimer started
  → All subsequent calls → PendingQueue

FlushTimer fires every FlushIntervalSeconds:
  → PendingQueue drained → grouped by TransactionGroupId → DispatchBatch per group

Queue reaches FlushThresholdPercent of MaxQueueSize:
  → Immediate flush triggered — bypasses timer at the cost of a spike
  → Prevents data loss under sustained high event throughput

MaxQueueSize exceeded:
  → Oldest entries dropped
  → UE_LOG Warning fired once per overflow event
  → New entries continue to enqueue

Flush() (graceful shutdown):
  → Timer cleared → PendingQueue drained immediately
  → Called automatically by UGameCoreBackendSubsystem::Deinitialize()
```

---

## Null Fallback Implementation

```cpp
class GAMECORE_API FNullAuditService : public IAuditService
{
public:
    virtual bool Connect(const FString&) override { return true; }
    virtual void Flush() override {}
    virtual void SetServerId(const FString&) override {}

    virtual void RecordEvent(const FAuditEntry& Entry) override
    {
        UE_LOG(LogGameCore, Log,
            TEXT("[NullAudit] Event [%s] Actor=%s: %s"),
            *Entry.EventTag.ToString(),
            *Entry.ActorId.ToString(),
            *Entry.Payload);
    }

    virtual void RecordBatch(const TArray<FAuditEntry>& Entries, bool) override
    {
        for (const FAuditEntry& Entry : Entries)
            RecordEvent(Entry);
    }
};
```

---

## FAuditPayloadBuilder

Utility struct for safe JSON construction. Avoids raw `FString::Printf` formatting at call sites. Lives in `GameCore` as a standalone utility — not tied to the interface.

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

---

## Usage Example — Market Trade

```cpp
void UMarketSystem::AuditTrade(
    const FGuid& BuyerActorId,   const FString& BuyerName,
    const FGuid& ListingId,
    int32        Price,
    const FGuid& SessionId)
{
    FAuditPayloadBuilder Builder;
    Builder.SetInt   (TEXT("price"),    Price)
           .SetString(TEXT("currency"), TEXT("Gold"))
           .SetGuid  (TEXT("listing"),  ListingId);

    FAuditEntry Entry;
    Entry.EventTag          = TAG_Audit_Market_Trade;
    Entry.SchemaVersion     = 1;
    Entry.ActorId           = BuyerActorId;
    Entry.ActorDisplayName  = BuyerName;
    Entry.SubjectId         = ListingId;
    Entry.SubjectTag        = TAG_Subject_Market_Listing;
    Entry.SessionId         = SessionId;
    Entry.Payload           = Builder.ToString();

    Backend->GetAudit(TEXT("Gameplay"))->RecordEvent(Entry);
}
```

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

- `InstanceGUID`, `ServerId`, and `Timestamp` are always stamped internally — callers must never set these.
- `SetServerId()` must be called before any events are dispatched. Until it is, all events queue silently. A one-time `UE_LOG Warning` fires if the queue reaches 50% capacity while `ServerId` is unset.
- `SessionId` is optional. If empty, the field is omitted from the dispatched record — not an error.
- `bTransactional = true` in `RecordBatch` guarantees atomic delivery only if the backend implementation supports it. Implementations that cannot honor transactionality must document this clearly.
- `Connect()` is public for interface technical reasons but **must only be called by `UGameCoreBackendSubsystem`**.
- `Flush()` is called automatically by `UGameCoreBackendSubsystem::Deinitialize()` — implementors do not need to call it manually on shutdown.
- **Abstraction vs implementation:** `FAuditServiceBase` defines queue ownership and flush policy. Retry logic, connection pooling, and threading are the responsibility of the **concrete implementation** (e.g. `FDatadogAuditService`). `MaxBatchSize` is a configuration hint — concrete implementations must respect it by splitting large flushes into multiple `DispatchBatch` calls rather than sending unbounded payloads to the backend in a single call.
- This interface is **server-side only**. Never instantiate or call from client code.
