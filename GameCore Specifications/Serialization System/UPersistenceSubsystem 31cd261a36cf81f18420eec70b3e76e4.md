# UPersistenceSubsystem

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
| --- | --- |
| `0` | Every cycle is a Full save |
| `3` | Partial, Partial, Partial, Full, Partial... |
| `9` | 9 Partials per Full (default) |

---

## Responsibilities

- Maintain `RegisteredEntities` — source of truth for all persistent actors
- Maintain `DirtySet` — GUIDs of actors with unsaved changes
- Maintain `SaveQueue` — pre-serialized payloads for actors that died dirty
- Drive two timers: periodic save cycle and DB queue drain
- On partial cycles: process dirty actors within per-tick budget
- On full cycles: serialize all registered actors regardless of dirty state
- Stamp every payload with `EPayloadType` and `ESerializationReason`
- Route payloads to the correct `FGameplayTag` delegate
- On single-actor events (logout, zone transfer, trigger): immediate full save
- On server shutdown: full save all actors synchronously, drain immediately
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

    // Seconds between each save cycle (partial or full).
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    float SaveInterval = 300.f;

    // Number of partial saves between full saves.
    // 0 = every cycle is a full save.
    // 9 = 9 partials then 1 full (default).
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    int32 PartialSavesBetweenFullSave = 9;

    // How often the SaveQueue is drained and broadcast (seconds).
    // Should be <= SaveInterval.
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    float DBFlushInterval = 60.f;

    // Max actors serialized per partial-cycle tick (frame spike control).
    // Does not apply to full cycles.
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Performance")
    int32 ActorsPerFlushTick = 100;

    // SaveQueue capacity cap. When exceeded a warning is logged via ILoggingService.
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Performance")
    int32 MaxSaveQueueSize = 1000;

    // --- Tag Delegate Registration ---

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnPayloadReady,
        const FEntityPersistencePayload&);

    // Register a tag and its delegate slot.
    // Must be called before any actor with this tag registers (before BeginPlay).
    void RegisterPersistenceTag(FGameplayTag Tag);

    // Retrieve the save delegate for a tag to bind transport handlers.
    FOnPayloadReady* GetSaveDelegate(FGameplayTag Tag);

    // --- Load API ---

    DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLoadRequested,
        FGuid /*EntityId*/, FGameplayTag /*Tag*/);
    FOnLoadRequested OnLoadRequested;

    void RequestLoad(FGuid EntityId, FGameplayTag Tag,
        TFunction<void(bool bSuccess)> OnComplete);

    void OnRawPayloadReceived(AActor* Actor,
        const FEntityPersistencePayload& Payload);

    // --- Event-Based Save API ---

    // Full save for a single actor. Used for ZoneTransfer, Logout, trigger events.
    void RequestFullSave(AActor* Entity, ESerializationReason Reason);

    // Full save of ALL registered entities on server shutdown.
    // Serializes and dispatches synchronously. Call before world teardown.
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
    TMap<FGuid, TWeakObjectPtr<UPersistenceRegistrationComponent>> RegisteredEntities;
    TSet<FGuid> DirtySet;
    TMap<FGuid, FEntityPersistencePayload> SaveQueue;
    TMap<FGameplayTag, FOnPayloadReady> TagDelegates;
    TMap<FGuid, TFunction<void(bool)>> LoadCallbacks;

    uint32 SaveCounter = 0; // Increments every cycle, never resets

    FTimerHandle SaveTimer;
    FTimerHandle DBFlushTimer;

    void FlushSaveCycle();
    void FlushSaveQueue();
    void DispatchPayload(const FEntityPersistencePayload& Payload);

    // Helper: safe logging via ILoggingService, falls back to UE_LOG
    void LogWarning(const FString& Message);
    void LogError(const FString& Message);

    static FGuid ServerInstanceId;
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

    ServerInstanceId = FGuid::NewGuid(); // Replace with stable env ID in production

    FTimerManager& TM = GetWorld()->GetTimerManager();

    TM.SetTimer(SaveTimer, this,
        &UPersistenceSubsystem::FlushSaveCycle,
        SaveInterval, true);

    TM.SetTimer(DBFlushTimer, this,
        &UPersistenceSubsystem::FlushSaveQueue,
        DBFlushInterval, true);
}
```

---

## Logging Helper

Centralizes the fallback pattern so call sites stay clean:

```cpp
void UPersistenceSubsystem::LogWarning(const FString& Message)
{
    if (auto* Backend = GetGameInstance()->GetSubsystem<UGameCoreBackendSubsystem>())
        Backend->GetLogging()->LogWarning(TEXT("Persistence"), Message);
    else
        UE_LOG(LogPersistence, Warning, TEXT("%s"), *Message);
}
```

> `GetLogging()` itself never returns null — it returns `FNullLoggingService` if not connected. The outer `if` guards against `UGameCoreBackendSubsystem` not being present at all (e.g. client builds or PIE without the subsystem).
> 

---

## FlushSaveCycle — Cycle Decision

```cpp
void UPersistenceSubsystem::FlushSaveCycle()
{
    ++SaveCounter;

    const bool bFullSave =
        (SaveCounter % (PartialSavesBetweenFullSave + 1) == 0);

    TArray<FEntityPersistencePayload> ReadyPayloads;
    TArray<FGuid> ToRemove;

    if (bFullSave)
    {
        // Full cycle: serialize all registered entities
        for (auto& [ID, RegCompPtr] : RegisteredEntities)
        {
            if (!RegCompPtr.IsValid()) continue;
            FEntityPersistencePayload Payload = RegCompPtr->BuildPayload(true);
            Payload.PayloadType = EPayloadType::Full;
            Payload.SaveReason  = ESerializationReason::Periodic;
            ReadyPayloads.Add(MoveTemp(Payload));
            ToRemove.Add(ID); // Clear from DirtySet — covered by full save
        }
    }
    else
    {
        // Partial cycle: only dirty actors, within budget
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
    }

    for (const FGuid& ID : ToRemove)
        DirtySet.Remove(ID);

    if (ReadyPayloads.IsEmpty()) return;

    Async(EAsyncExecution::ThreadPool,
        [Payloads = MoveTemp(ReadyPayloads), this]()
        {
            AsyncTask(ENamedThreads::GameThread, [this, Payloads]()
            {
                for (auto& Payload : Payloads)
                    DispatchPayload(Payload);
            });
        });
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

```cpp
void UPersistenceSubsystem::RequestShutdownSave()
{
    GetWorld()->GetTimerManager().ClearTimer(SaveTimer);

    for (auto& [ID, RegCompPtr] : RegisteredEntities)
    {
        if (!RegCompPtr.IsValid()) continue;

        FEntityPersistencePayload Payload = RegCompPtr->BuildPayload(true);
        Payload.PayloadType = EPayloadType::Full;
        Payload.SaveReason  = ESerializationReason::ServerShutdown;

        SaveQueue.Add(ID, MoveTemp(Payload));
    }

    // Synchronous drain — transport bindings must handle this context
    for (auto& [ID, Payload] : SaveQueue)
        DispatchPayload(Payload);

    SaveQueue.Empty();
    DirtySet.Empty();
}
```

> `RequestShutdownSave` dispatches synchronously. Transport bindings must not assume async context.
> 

---

## FlushSaveQueue — Dead Actor Drain

```cpp
void UPersistenceSubsystem::FlushSaveQueue()
{
    if (SaveQueue.IsEmpty()) return;

    TArray<FEntityPersistencePayload> Payloads;
    SaveQueue.GenerateValueArray(Payloads);
    SaveQueue.Empty();

    Async(EAsyncExecution::ThreadPool,
        [Payloads = MoveTemp(Payloads), this]()
        {
            AsyncTask(ENamedThreads::GameThread, [this, Payloads]()
            {
                for (auto& Payload : Payloads)
                    DispatchPayload(Payload);
            });
        });
}
```

---

## MoveToSaveQueue — With Capacity Alert

```cpp
void UPersistenceSubsystem::MoveToSaveQueue(FGuid EntityId,
    FEntityPersistencePayload Payload)
{
    if (SaveQueue.Num() >= MaxSaveQueueSize)
    {
        LogWarning(FString::Printf(
            TEXT("SaveQueue at capacity (%d). Payload for [%s] dropped."),
            MaxSaveQueueSize, *EntityId.ToString()));
        return;
    }

    DirtySet.Remove(EntityId);
    SaveQueue.Add(EntityId, MoveTemp(Payload));
}
```

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

## Load Path

```cpp
void UPersistenceSubsystem::RequestLoad(FGuid EntityId, FGameplayTag Tag,
    TFunction<void(bool bSuccess)> OnComplete)
{
    LoadCallbacks.Add(EntityId, MoveTemp(OnComplete));
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

    if (auto* Callback = LoadCallbacks.Find(Payload.EntityId))
    {
        (*Callback)(true);
        LoadCallbacks.Remove(Payload.EntityId);
    }
}
```

---

## ESerializationReason & EPayloadType

```cpp
UENUM()
enum class ESerializationReason : uint8
{
    Periodic,        // Timer-driven cycle
    ZoneTransfer,    // Actor moving between servers — always Full
    Logout,          // Player disconnect — always Full, immediate flush
    CriticalEvent,   // Explicit trigger — game-defined
    ServerShutdown,  // Server shutting down — all actors, synchronous
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
    FName Key;
    uint32 Version;
    TArray<uint8> Data;
};

USTRUCT()
struct GAMECORE_API FEntityPersistencePayload
{
    GENERATED_BODY()
    FGuid EntityId;
    FGuid ServerInstanceId;
    FGameplayTag PersistenceTag;
    EPayloadType PayloadType         = EPayloadType::Partial;
    ESerializationReason SaveReason  = ESerializationReason::Periodic;
    int64 Timestamp                  = 0;
    TArray<FComponentPersistenceBlob> Components;
};
```

---

## File Structure

```jsx
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

- `DispatchPayload` fires on game thread during async flushes. During `RequestShutdownSave` it is synchronous — transport bindings must handle both.
- Full cycles serialize **all** registered entities — `ActorsPerFlushTick` has no effect. Profile full cycle duration under expected server load.
- `SaveCounter` is `uint32` and wraps at ~4 billion cycles. At one cycle per 300s this takes ~40,000 years. Not a concern.
- `ServerInstanceId` must be sourced from a stable environment variable in production to survive server restarts.
- Only one transport layer should bind per tag's `OnLoadRequested` — multiple bindings cause duplicate fetch attempts.

---

## Future Improvements

- Per-tag `ActorsPerFlushTick` budgets to prevent player saves being starved by mobs
- Full-cycle spreading across multiple ticks to avoid frame spikes at scale
- Load error path — `OnComplete(false)` currently never fires
- `CriticalEvent` component key filter on `RequestFullSave`
- PIE teardown guard in `EndPlay`
- Metrics hooks: payload size, queue depth, flush latency, dropped payload counter