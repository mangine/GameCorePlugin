# UPersistenceSubsystem

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Persistence/PersistenceSubsystem.h`
**Type:** `UGameInstanceSubsystem`

Central coordinator of the persistence system. Owns all timing, actor registries, the dirty set, and the save queue. Produces `FEntityPersistencePayload` blobs stamped as **Partial** or **Full** and routes them via `FGameplayTag`-keyed delegates. Has **no knowledge of storage backends** — transport is external.

---

## Backend Compatibility

`UPersistenceSubsystem` does **not** depend on `UGameCoreBackendSubsystem`. Logging calls use `ILoggingService` via a subsystem lookup — if not present or not connected, it falls back to `UE_LOG` transparently.

Storage wiring is opt-in: bind `FOnPayloadReady` delegates to `IDBService::Set` in the game module. See **GameCore Backend → IDBService** for the wiring example.

---

## Save Cycle Logic

A single `SaveCounter` increments on every timer expiration. Whether a cycle is partial or full is determined by modulo — no reset logic required:

```cpp
++SaveCounter;
bool bFullSave = (SaveCounter % (PartialSavesBetweenFullSave + 1) == 0);
```

| `PartialSavesBetweenFullSave` | Behavior |
|---|---|
| `0` | Every cycle is a Full save |
| `3` | Partial, Partial, Partial, Full, Partial... |
| `9` | 9 Partials per Full (default) |

**Full cycles** are spread across multiple ticks using a cursor. `ActorsPerFlushTick` applies uniformly to both partial and full cycles — no frame spikes at scale.

---

## Responsibilities

- Maintain `RegisteredEntities` — source of truth for all persistent actors
- Maintain `DirtySet` — GUIDs of actors with unsaved changes
- Maintain `PrioritySaveQueue` — critical payloads (Logout, ZoneTransfer, ServerShutdown), never dropped
- Maintain `SaveQueue` — normal dead-actor payloads, bounded by `MaxSaveQueueSize`
- Drive timers: periodic save cycle, DB queue drain, load timeout sweep
- On partial cycles: process dirty actors within per-tick budget (`ActorsPerFlushTick`)
- On full cycles: spread all registered actors across ticks using `ActorsPerFlushTick` budget
- Stamp every payload with `EPayloadType` and `ESerializationReason`
- Route payloads to the correct `FGameplayTag` delegate
- On single-actor events (logout, zone transfer, trigger): immediate full save
- On server shutdown: full save all actors synchronously, drain immediately
- Fire `OnComplete(false)` on load failure or timeout — never leave callbacks dangling
- Log capacity alerts via `ILoggingService` (falls back to `UE_LOG` if not wired)

---

## Class Declaration

```cpp
UCLASS(Config=Game)
class GAMECORE_API UPersistenceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // --- Configuration ---

    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    float SaveInterval = 300.f;

    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    int32 PartialSavesBetweenFullSave = 9;

    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    float DBFlushInterval = 60.f;

    // Applies to BOTH partial and full cycles.
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Performance")
    int32 ActorsPerFlushTick = 100;

    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Performance")
    int32 MaxSaveQueueSize = 1000;

    // When SaveQueue reaches this fraction of MaxSaveQueueSize, FlushSaveQueue is triggered
    // immediately rather than waiting for DBFlushInterval. Prevents data loss under burst load
    // at the cost of a spike.
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Performance")
    float DBFlushThresholdPercent = 0.75f;

    // Maximum number of normal-priority payloads dispatched per FlushSaveQueue call.
    // Priority payloads are always fully drained regardless of this budget.
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Performance")
    int32 MaxPayloadsPerDBFlush = 200;

    // Seconds before a pending load callback fires OnComplete(false).
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Load")
    float LoadTimeoutSeconds = 30.f;

    // Stable server identity stamped into every FEntityPersistencePayload.
    // Must be set via DefaultGame.ini or a UGameInstance override before Initialize() is called.
    // If unset, a UE_LOG Error is emitted. Payloads stamped with an unstable GUID cannot be
    // deduplicated across server restarts — this matters for audit and rollback tooling.
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Identity")
    FGuid ServerInstanceId;

    // --- Tag Delegate Registration ---

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnPayloadReady,
        const FEntityPersistencePayload&);

    void RegisterPersistenceTag(FGameplayTag Tag);
    FOnPayloadReady* GetSaveDelegate(FGameplayTag Tag);

    // --- Load API ---

    DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLoadRequested,
        FGuid /*EntityId*/, FGameplayTag /*Tag*/);
    FOnLoadRequested OnLoadRequested;

    void RequestLoad(FGuid EntityId, FGameplayTag Tag,
        TFunction<void(bool bSuccess)> OnComplete);

    void OnRawPayloadReceived(AActor* Actor,
        const FEntityPersistencePayload& Payload);

    // Called by transport on DB fetch failure.
    void OnLoadFailed(FGuid EntityId);

    // --- Event-Based Save API ---

    void RequestFullSave(AActor* Entity, ESerializationReason Reason);
    void RequestShutdownSave();

    // --- Internal API (called by UPersistenceRegistrationComponent) ---

    void RegisterEntity(UPersistenceRegistrationComponent* RegComp);
    void UnregisterEntity(FGuid EntityId);
    void EnqueueDirty(FGuid EntityId);
    void MoveToSaveQueue(FGuid EntityId, FEntityPersistencePayload Payload);

    static FGuid GetServerInstanceId();

protected:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

private:
    // --- Registries ---
    TMap<FGuid, TWeakObjectPtr<UPersistenceRegistrationComponent>> RegisteredEntities;
    TSet<FGuid> DirtySet;

    // Priority queue: Logout / ZoneTransfer / ServerShutdown — never dropped.
    // Keyed by EntityId: one entry per entity. A newer save for the same entity overwrites the older one.
    // This is intentional — we want the most recent state, not duplicate saves of the same actor.
    TMap<FGuid, FEntityPersistencePayload> PrioritySaveQueue;

    // Normal queue: bounded by MaxSaveQueueSize.
    TMap<FGuid, FEntityPersistencePayload> SaveQueue;

    TMap<FGameplayTag, FOnPayloadReady> TagDelegates;

    // --- Load Callbacks ---
    struct FLoadRequest
    {
        TFunction<void(bool)> Callback;
        float Timestamp; // FPlatformTime::Seconds() at enqueue time
    };
    TMap<FGuid, FLoadRequest> LoadCallbacks;

    // --- Cycle State ---
    uint32 SaveCounter = 0;

    // Full-cycle spread state
    bool          bFullCycleInProgress  = false;
    int32         FullCycleCursorIndex  = 0;
    TArray<FGuid> FullCycleEntitySnapshot;

    // --- Timers ---
    FTimerHandle SaveTimer;
    FTimerHandle DBFlushTimer;
    FTimerHandle FullCycleTickTimer;
    FTimerHandle LoadTimeoutTimer;

    // --- Internal Methods ---
    void FlushSaveCycle();
    void FlushPartialCycle();
    void TickFullCycle();
    void FlushSaveQueue();
    void DispatchPayload(const FEntityPersistencePayload& Payload);
    void TickLoadTimeouts();

    static bool IsCriticalReason(ESerializationReason Reason);

    void LogWarning(const FString& Message);
    void LogError(const FString& Message);
};
```

---

## ShouldCreateSubsystem — Server Only

```cpp
bool UPersistenceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    UGameInstance* GI = Cast<UGameInstance>(Outer);
    if (!GI) return false;
    UWorld* World = GI->GetWorld();
    if (!World) return false;
    ENetMode NetMode = World->GetNetMode();
    return NetMode == NM_DedicatedServer || NetMode == NM_ListenServer;
}
```

---

## Initialization

```cpp
void UPersistenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (!ServerInstanceId.IsValid())
        UE_LOG(LogPersistence, Error,
            TEXT("[Persistence] ServerInstanceId is not configured. "
                 "Set it via DefaultGame.ini before Initialize. "
                 "Payloads stamped with an invalid GUID cannot be deduplicated across restarts."));

    FTimerManager& TM = GetWorld()->GetTimerManager();

    TM.SetTimer(SaveTimer, this,
        &UPersistenceSubsystem::FlushSaveCycle,
        SaveInterval, true);

    TM.SetTimer(DBFlushTimer, this,
        &UPersistenceSubsystem::FlushSaveQueue,
        DBFlushInterval, true);

    TM.SetTimer(LoadTimeoutTimer, this,
        &UPersistenceSubsystem::TickLoadTimeouts,
        5.f, true);
}
```

---

## FlushSaveCycle — Cycle Decision

```cpp
void UPersistenceSubsystem::FlushSaveCycle()
{
    ++SaveCounter;
    const bool bFullSave = (SaveCounter % (PartialSavesBetweenFullSave + 1) == 0);

    if (bFullSave)
    {
        // Snapshot entity list and begin spreading across ticks
        RegisteredEntities.GetKeys(FullCycleEntitySnapshot);
        FullCycleCursorIndex = 0;
        bFullCycleInProgress = true;

        // Fire every frame until complete
        GetWorld()->GetTimerManager().SetTimer(
            FullCycleTickTimer, this,
            &UPersistenceSubsystem::TickFullCycle,
            0.0f, true);
    }
    else
    {
        FlushPartialCycle();
    }
}
```

---

## TickFullCycle — Spread Full Save Across Ticks

```cpp
void UPersistenceSubsystem::TickFullCycle()
{
    int32 Processed = 0;
    TArray<FEntityPersistencePayload> ReadyPayloads;

    while (FullCycleCursorIndex < FullCycleEntitySnapshot.Num()
           && Processed < ActorsPerFlushTick)
    {
        const FGuid& ID = FullCycleEntitySnapshot[FullCycleCursorIndex++];
        auto* RegCompPtr = RegisteredEntities.Find(ID);
        if (!RegCompPtr || !RegCompPtr->IsValid()) continue;

        FEntityPersistencePayload Payload = RegCompPtr->Get()->BuildPayload(true);
        Payload.PayloadType = EPayloadType::Full;
        Payload.SaveReason  = ESerializationReason::Periodic;
        ReadyPayloads.Add(MoveTemp(Payload));
        DirtySet.Remove(ID);
        Processed++;
    }

    for (auto& Payload : ReadyPayloads)
        DispatchPayload(Payload);

    if (FullCycleCursorIndex >= FullCycleEntitySnapshot.Num())
    {
        bFullCycleInProgress = false;
        FullCycleEntitySnapshot.Reset();
        GetWorld()->GetTimerManager().ClearTimer(FullCycleTickTimer);
    }
}
```

---

## FlushPartialCycle — Dirty Actors Within Budget

```cpp
void UPersistenceSubsystem::FlushPartialCycle()
{
    TArray<FEntityPersistencePayload> ReadyPayloads;
    TArray<FGuid> ToRemove;
    int32 Processed = 0;

    for (const FGuid& ID : DirtySet)
    {
        if (Processed++ >= ActorsPerFlushTick) break;

        auto* RegCompPtr = RegisteredEntities.Find(ID);
        if (!RegCompPtr || !RegCompPtr->IsValid())
        {
            ToRemove.Add(ID);
            continue;
        }

        FEntityPersistencePayload Payload = RegCompPtr->Get()->BuildPayload(false);
        Payload.PayloadType = EPayloadType::Partial;
        Payload.SaveReason  = ESerializationReason::Periodic;
        ReadyPayloads.Add(MoveTemp(Payload));
        ToRemove.Add(ID);
    }

    for (const FGuid& ID : ToRemove)
        DirtySet.Remove(ID);

    for (auto& Payload : ReadyPayloads)
        DispatchPayload(Payload);
}
```

---

## RequestFullSave — Single Actor Event

```cpp
void UPersistenceSubsystem::RequestFullSave(AActor* Entity,
    ESerializationReason Reason)
{
    auto* RegComp =
        Entity->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (!RegComp) return;

    FEntityPersistencePayload Payload = RegComp->BuildPayload(true);
    Payload.PayloadType = EPayloadType::Full;
    Payload.SaveReason  = Reason;

    MoveToSaveQueue(RegComp->GetEntityGUID(), MoveTemp(Payload));

    if (Reason == ESerializationReason::Logout ||
        Reason == ESerializationReason::ServerShutdown)
    {
        FlushSaveQueue(); // Drain immediately
    }
}
```

---

## RequestShutdownSave — All Actors

Synchronous. Cancels all timers and serializes everything immediately. Drains both queues.

```cpp
void UPersistenceSubsystem::RequestShutdownSave()
{
    FTimerManager& TM = GetWorld()->GetTimerManager();
    TM.ClearTimer(SaveTimer);
    TM.ClearTimer(FullCycleTickTimer);

    bFullCycleInProgress = false;
    FullCycleEntitySnapshot.Reset();

    for (auto& [ID, RegCompPtr] : RegisteredEntities)
    {
        if (!RegCompPtr.IsValid()) continue;

        FEntityPersistencePayload Payload = RegCompPtr->BuildPayload(true);
        Payload.PayloadType = EPayloadType::Full;
        Payload.SaveReason  = ESerializationReason::ServerShutdown;

        // Always goes to priority queue on shutdown
        PrioritySaveQueue.Add(ID, MoveTemp(Payload));
    }

    // Drain priority first, then normal
    for (auto& [ID, Payload] : PrioritySaveQueue)
        DispatchPayload(Payload);
    PrioritySaveQueue.Empty();

    for (auto& [ID, Payload] : SaveQueue)
        DispatchPayload(Payload);
    SaveQueue.Empty();

    DirtySet.Empty();
}
```

---

## MoveToSaveQueue — Priority vs Normal

Critical reasons (`Logout`, `ZoneTransfer`, `ServerShutdown`) are never dropped and go to `PrioritySaveQueue`. All other reasons respect the `MaxSaveQueueSize` cap.

`PrioritySaveQueue` is keyed by `EntityId` — a newer save for the same entity overwrites the older one. This is intentional: we always want the most recent state of an entity, not duplicate saves.

```cpp
bool UPersistenceSubsystem::IsCriticalReason(ESerializationReason Reason)
{
    return Reason == ESerializationReason::Logout
        || Reason == ESerializationReason::ZoneTransfer
        || Reason == ESerializationReason::ServerShutdown;
}

void UPersistenceSubsystem::MoveToSaveQueue(FGuid EntityId,
    FEntityPersistencePayload Payload)
{
    DirtySet.Remove(EntityId);

    if (IsCriticalReason(Payload.SaveReason))
    {
        // Critical: always accepted, overwrites duplicate if re-queued
        PrioritySaveQueue.Add(EntityId, MoveTemp(Payload));
        return;
    }

    if (SaveQueue.Num() >= MaxSaveQueueSize)
    {
        LogWarning(FString::Printf(
            TEXT("SaveQueue at capacity (%d). Non-critical payload for [%s] dropped."),
            MaxSaveQueueSize, *EntityId.ToString()));
        return;
    }

    SaveQueue.Add(EntityId, MoveTemp(Payload));

    // Pressure flush: if SaveQueue crosses the threshold, drain immediately
    // rather than waiting for the next DBFlushInterval tick.
    const int32 Threshold = FMath::FloorToInt(MaxSaveQueueSize * DBFlushThresholdPercent);
    if (SaveQueue.Num() >= Threshold)
        FlushSaveQueue();
}
```

---

## FlushSaveQueue — Priority-First Drain with Chunking

Drains all priority entries first (always fully), then normal entries up to `MaxPayloadsPerDBFlush` per call. If normal entries remain after the budget is exhausted, the timer fires again next interval to continue draining.

Called periodically by `DBFlushTimer` and immediately by the pressure-flush path in `MoveToSaveQueue`.

```cpp
void UPersistenceSubsystem::FlushSaveQueue()
{
    if (PrioritySaveQueue.IsEmpty() && SaveQueue.IsEmpty()) return;

    // Priority payloads — always fully drained, no budget limit
    for (auto It = PrioritySaveQueue.CreateIterator(); It; ++It)
    {
        DispatchPayload(It->Value);
        It.RemoveCurrent();
    }

    // Normal payloads — respect MaxPayloadsPerDBFlush budget
    int32 Dispatched = 0;
    for (auto It = SaveQueue.CreateIterator(); It && Dispatched < MaxPayloadsPerDBFlush; ++It)
    {
        DispatchPayload(It->Value);
        It.RemoveCurrent();
        ++Dispatched;
    }
}
```

---

## Load Path

```cpp
void UPersistenceSubsystem::RequestLoad(FGuid EntityId, FGameplayTag Tag,
    TFunction<void(bool)> OnComplete)
{
    LoadCallbacks.Add(EntityId, {
        MoveTemp(OnComplete),
        (float)FPlatformTime::Seconds()
    });
    OnLoadRequested.Broadcast(EntityId, Tag);
}

void UPersistenceSubsystem::OnRawPayloadReceived(AActor* Actor,
    const FEntityPersistencePayload& Payload)
{
    auto* RegComp =
        Actor->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (!RegComp) return;

    for (const FComponentPersistenceBlob& Blob : Payload.Components)
    {
        IPersistableComponent* Target = nullptr;
        for (auto& Ref : RegComp->GetCachedPersistables())
        {
            if (Ref.GetInterface()->GetPersistenceKey() == Blob.Key)
            {
                Target = Ref.GetInterface();
                break;
            }
        }
        if (!Target) continue;

        FMemoryReader Reader(Blob.Data);
        const uint32 SavedVersion   = Blob.Version;
        const uint32 CurrentVersion = Target->GetSchemaVersion();

        if (SavedVersion != CurrentVersion)
            Target->Migrate(Reader, SavedVersion, CurrentVersion);

        Target->Serialize_Load(Reader, SavedVersion);
    }

    if (auto* Request = LoadCallbacks.Find(Payload.EntityId))
    {
        Request->Callback(true);
        LoadCallbacks.Remove(Payload.EntityId);
    }
}

// Transport calls this when a DB fetch fails.
void UPersistenceSubsystem::OnLoadFailed(FGuid EntityId)
{
    if (auto* Request = LoadCallbacks.Find(EntityId))
    {
        LogError(FString::Printf(
            TEXT("Load failed for entity [%s]."), *EntityId.ToString()));
        Request->Callback(false);
        LoadCallbacks.Remove(EntityId);
    }
}
```

---

## Load Timeout Sweep

Fires every 5 seconds. Any pending callback older than `LoadTimeoutSeconds` is fired with `false` and removed — no dangling callbacks.

```cpp
void UPersistenceSubsystem::TickLoadTimeouts()
{
    const float Now = (float)FPlatformTime::Seconds();
    TArray<FGuid> Expired;

    for (auto& [ID, Request] : LoadCallbacks)
    {
        if (Now - Request.Timestamp >= LoadTimeoutSeconds)
            Expired.Add(ID);
    }

    for (const FGuid& ID : Expired)
    {
        LogError(FString::Printf(
            TEXT("Load timed out for entity [%s]. Firing OnComplete(false)."),
            *ID.ToString()));
        LoadCallbacks[ID].Callback(false);
        LoadCallbacks.Remove(ID);
    }
}
```

> The game module defines what `OnComplete(false)` does — kick the player, show an error screen, queue a retry. The subsystem guarantees the callback always fires.

---

## Tag Registration & Routing

```cpp
void UPersistenceSubsystem::RegisterPersistenceTag(FGameplayTag Tag)
{
    if (!TagDelegates.Contains(Tag))
        TagDelegates.Add(Tag, FOnPayloadReady());
}

FOnPayloadReady* UPersistenceSubsystem::GetSaveDelegate(FGameplayTag Tag)
{
    return TagDelegates.Find(Tag);
}

void UPersistenceSubsystem::DispatchPayload(const FEntityPersistencePayload& Payload)
{
    FOnPayloadReady* Delegate = TagDelegates.Find(Payload.PersistenceTag);
    if (!Delegate)
    {
        LogError(FString::Printf(
            TEXT("No delegate for tag [%s]. Payload for [%s] dropped."),
            *Payload.PersistenceTag.ToString(),
            *Payload.EntityId.ToString()));
        return;
    }
    Delegate->Broadcast(Payload);
}
```

Startup binding example:

```cpp
Subsystem->RegisterPersistenceTag(TAG_Persistence_Entity_Player);
Subsystem->GetSaveDelegate(TAG_Persistence_Entity_Player)
    ->AddUObject(MyTransport, &UMyTransport::OnPlayerPayloadReady);
```

---

## Logging Helper

```cpp
void UPersistenceSubsystem::LogWarning(const FString& Message)
{
    if (auto* Backend = GetGameInstance()->GetSubsystem<UGameCoreBackendSubsystem>())
        Backend->GetLogging()->LogWarning(TEXT("Persistence"), Message);
    else
        UE_LOG(LogPersistence, Warning, TEXT("%s"), *Message);
}
```

> `GetLogging()` never returns null — it returns `FNullLoggingService` if not connected. The outer `if` guards against `UGameCoreBackendSubsystem` not being present at all (e.g. client builds or PIE).

---

## ESerializationReason & EPayloadType

```cpp
UENUM()
enum class ESerializationReason : uint8
{
    Periodic,        // Timer-driven cycle
    ZoneTransfer,    // Actor moving between servers — always Full, never dropped
    Logout,          // Player disconnect — always Full, immediate flush, never dropped
    CriticalEvent,   // Explicit trigger — game-defined
    ServerShutdown,  // Server shutting down — all actors, synchronous, never dropped
};

UENUM()
enum class EPayloadType : uint8
{
    Partial, // Dirty components only. Must be applied on top of a prior Full.
    Full,    // All components. Standalone — no prior state required.
};
```

---

## FEntityPersistencePayload & Supporting Types

Declared in `PersistenceTypes.h`.

```cpp
USTRUCT()
struct GAMECORE_API FComponentPersistenceBlob
{
    GENERATED_BODY()
    FName         Key;
    uint32        Version;
    TArray<uint8> Data;
};

USTRUCT()
struct GAMECORE_API FEntityPersistencePayload
{
    GENERATED_BODY()
    FGuid                     EntityId;
    FGuid                     ServerInstanceId;
    FGameplayTag              PersistenceTag;
    EPayloadType              PayloadType  = EPayloadType::Partial;
    ESerializationReason      SaveReason   = ESerializationReason::Periodic;
    int64                     Timestamp    = 0;
    TArray<FComponentPersistenceBlob> Components;
};
```

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Persistence/
            ├── PersistenceTypes.h
            ├── PersistableComponent.h/.cpp
            ├── PersistenceRegistrationComponent.h/.cpp
            └── PersistenceSubsystem.h/.cpp
```

---

## Notes & Caveats

- `ActorsPerFlushTick` applies to **both** partial and full cycles. Full cycles snapshot `RegisteredEntities` at cycle start and spread across frames via `FullCycleTickTimer`.
- If a new actor registers mid-full-cycle it will be picked up on the next full cycle — this is acceptable.
- `RequestShutdownSave` is synchronous and bypasses the tick budget. Transport bindings must handle synchronous dispatch.
- `PrioritySaveQueue` is keyed by `EntityId` — a newer save for the same entity overwrites the older one. This is intentional. The queue is logically bounded by the number of concurrently connected players.
- `LoadTimeoutSeconds` should exceed the worst-case DB round-trip. Default 30s is conservative.
- `SaveCounter` is `uint32` and wraps at ~4 billion cycles. At one cycle per 300s this is ~40,000 years.
- `ServerInstanceId` must be set via `DefaultGame.ini` or a `UGameInstance` override before `Initialize()`. If unset, a `UE_LOG Error` is emitted — payloads without a stable ID cannot be deduplicated across server restarts.
- Only one transport layer should bind per tag's `OnLoadRequested` — multiple bindings cause duplicate fetch attempts.

---

## Future Improvements

- Per-tag `ActorsPerFlushTick` budgets to prevent player saves being starved by mobs
- `CriticalEvent` component key filter on `RequestFullSave`
- PIE teardown guard in `EndPlay`
- Metrics hooks: payload size, queue depth, flush latency, dropped payload counter
- Load retry logic — currently `OnComplete(false)` fires and game module must handle retry; a built-in retry with backoff could be added here
