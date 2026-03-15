# UPersistenceSubsystem

**Module:** `GameCore`
**Location:** `GameCore/Source/GameCore/Persistence/PersistenceSubsystem.h`
**Type:** `UGameInstanceSubsystem`

Central coordinator of the persistence system. Owns all timing, actor registries, and the dirty set. Serializes actor component state into `FEntityPersistencePayload` blobs and routes them via `FGameplayTag`-keyed delegates. Has **no knowledge of storage backends** — transport is external.

The subsystem does **not** own a write-behind queue, flush timer toward the DB, or a priority queue. These responsibilities belong entirely to `IKeyStorageService`. The subsystem's only job is: decide *which actors* to serialize and *when*, produce payloads, and dispatch them immediately via delegates. The DB service absorbs each payload into its own write-behind queue and handles batching, priority, flush timing, retry, and deduplication from that point forward.

---

## Backend Compatibility

`UPersistenceSubsystem` does **not** depend on `UGameCoreBackendSubsystem`. Logging calls use `FGameCoreBackend::GetLogging()` — if the backend subsystem is not live, `FGameCoreBackend` falls back to `FNullLoggingService` which routes to `UE_LOG` transparently. No conditional check required at the call site.

Storage wiring is opt-in: bind `FOnPayloadReady` delegates to `IKeyStorageService::Set` in the game module. See **GameCore Backend → IKeyStorageService** for the wiring example.

---

## Responsibility Boundary

| Responsibility | Owner |
|---|---|
| Which actors to serialize and when | `UPersistenceSubsystem` |
| Dirty tracking, partial vs full cycles | `UPersistenceSubsystem` |
| Serializing component state into binary blobs | `UPersistenceSubsystem` |
| Dispatching payloads to transport delegates | `UPersistenceSubsystem` |
| Write-behind queue, flush timing, batching | `IKeyStorageService` |
| Priority lanes, critical entry guarantees | `IKeyStorageService` |
| Retry, reconnect, deduplication | `IKeyStorageService` |

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
- Drive timers: periodic save cycle and load timeout sweep
- On partial cycles: serialize dirty actors within per-tick budget (`ActorsPerFlushTick`)
- On full cycles: serialize all registered actors across ticks using `ActorsPerFlushTick` budget
- Stamp every payload with `EPayloadType`, `ESerializationReason`, `bCritical`, and `bFlushImmediately`
- Route payloads to the correct `FGameplayTag` delegate immediately — no intermediate queue
- On single-actor events (logout, zone transfer, trigger): immediate full save with `bCritical` and/or `bFlushImmediately` set
- On server shutdown: full save all registered entities synchronously
- Fire `OnComplete(false)` on load failure or timeout — never leave callbacks dangling
- Log capacity alerts and errors via `FGameCoreBackend::GetLogging()` (falls back to `UE_LOG` automatically)

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

    // Applies to BOTH partial and full cycles.
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Performance")
    int32 ActorsPerFlushTick = 100;

    // Seconds before a pending load callback fires OnComplete(false).
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Load")
    float LoadTimeoutSeconds = 30.f;

    // Stable server identity stamped into every FEntityPersistencePayload.
    // Must be set via DefaultGame.ini or a UGameInstance override before Initialize() is called.
    // If unset, a UE_LOG Error is emitted. Payloads stamped with an invalid GUID cannot be
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

protected:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

private:
    // --- Registries ---
    TMap<FGuid, TWeakObjectPtr<UPersistenceRegistrationComponent>> RegisteredEntities;
    TSet<FGuid> DirtySet;

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

    bool          bFullCycleInProgress  = false;
    int32         FullCycleCursorIndex  = 0;
    TArray<FGuid> FullCycleEntitySnapshot;

    // --- Timers ---
    FTimerHandle SaveTimer;
    FTimerHandle FullCycleTickTimer;
    FTimerHandle LoadTimeoutTimer;

    // --- Internal Methods ---
    void FlushSaveCycle();
    void FlushPartialCycle();
    void TickFullCycle();
    void DispatchPayload(const FEntityPersistencePayload& Payload);
    void TickLoadTimeouts();

    static bool IsCriticalReason(ESerializationReason Reason);
    static bool IsImmediateReason(ESerializationReason Reason);
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
        RegisteredEntities.GetKeys(FullCycleEntitySnapshot);
        FullCycleCursorIndex = 0;
        bFullCycleInProgress = true;

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

    while (FullCycleCursorIndex < FullCycleEntitySnapshot.Num()
           && Processed < ActorsPerFlushTick)
    {
        const FGuid& ID = FullCycleEntitySnapshot[FullCycleCursorIndex++];
        auto* RegCompPtr = RegisteredEntities.Find(ID);
        if (!RegCompPtr || !RegCompPtr->IsValid()) continue;

        FEntityPersistencePayload Payload = RegCompPtr->Get()->BuildPayload(true);
        Payload.PayloadType       = EPayloadType::Full;
        Payload.SaveReason        = ESerializationReason::Periodic;
        Payload.bCritical         = false;
        Payload.bFlushImmediately = false;
        DirtySet.Remove(ID);
        Processed++;

        DispatchPayload(Payload);
    }

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
        Payload.PayloadType       = EPayloadType::Partial;
        Payload.SaveReason        = ESerializationReason::Periodic;
        Payload.bCritical         = false;
        Payload.bFlushImmediately = false;
        ToRemove.Add(ID);

        DispatchPayload(Payload);
    }

    for (const FGuid& ID : ToRemove)
        DirtySet.Remove(ID);
}
```

---

## RequestFullSave — Single Actor Event

For critical reasons (Logout, ZoneTransfer, ServerShutdown), sets `bCritical = true` on the payload so the DB service places it in its priority lane. For Logout and ServerShutdown, also sets `bFlushImmediately = true` to bypass the DB queue entirely.

```cpp
bool UPersistenceSubsystem::IsCriticalReason(ESerializationReason Reason)
{
    return Reason == ESerializationReason::Logout
        || Reason == ESerializationReason::ZoneTransfer
        || Reason == ESerializationReason::ServerShutdown;
}

bool UPersistenceSubsystem::IsImmediateReason(ESerializationReason Reason)
{
    return Reason == ESerializationReason::Logout
        || Reason == ESerializationReason::ServerShutdown;
}

void UPersistenceSubsystem::RequestFullSave(AActor* Entity,
    ESerializationReason Reason)
{
    auto* RegComp =
        Entity->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (!RegComp) return;

    FEntityPersistencePayload Payload = RegComp->BuildPayload(true);
    Payload.PayloadType       = EPayloadType::Full;
    Payload.SaveReason        = Reason;
    Payload.bCritical         = IsCriticalReason(Reason);
    Payload.bFlushImmediately = IsImmediateReason(Reason);

    DirtySet.Remove(RegComp->GetEntityGUID());
    DispatchPayload(Payload);
}
```

---

## RequestShutdownSave — All Actors

Synchronous. Cancels all timers and serializes every entry in `RegisteredEntities` immediately. All payloads are marked `bCritical = true` and `bFlushImmediately = true`.

This is safe because `RegisteredEntities` is intentionally bounded: NPCs and Mobs are respawnable world state and **must not** register with the persistence system by default. The registered set should contain only players, ships, and key world-state objects — a count that comfortably completes within OS shutdown timeout (typically 10–30 s). If a new entity category is added and opts into persistence, its expected count at peak load must be evaluated against this constraint before it is allowed to register.

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
        Payload.PayloadType       = EPayloadType::Full;
        Payload.SaveReason        = ESerializationReason::ServerShutdown;
        Payload.bCritical         = true;
        Payload.bFlushImmediately = true;

        DispatchPayload(Payload);
    }

    DirtySet.Empty();
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

void UPersistenceSubsystem::OnLoadFailed(FGuid EntityId)
{
    if (auto* Request = LoadCallbacks.Find(EntityId))
    {
        FGameCoreBackend::GetLogging()->LogError(TEXT("Persistence"),
            FString::Printf(TEXT("Load failed for entity [%s]."), *EntityId.ToString()));
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
        FGameCoreBackend::GetLogging()->LogError(TEXT("Persistence"),
            FString::Printf(TEXT("Load timed out for entity [%s]. Firing OnComplete(false)."),
                *ID.ToString()));
        LoadCallbacks[ID].Callback(false);
        LoadCallbacks.Remove(ID);
    }
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
        FGameCoreBackend::GetLogging()->LogError(TEXT("Persistence"),
            FString::Printf(TEXT("No delegate for tag [%s]. Payload for [%s] dropped."),
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

## ESerializationReason & EPayloadType

```cpp
UENUM()
enum class ESerializationReason : uint8
{
    Periodic,        // Timer-driven cycle
    ZoneTransfer,    // Actor moving between servers — bCritical=true
    Logout,          // Player disconnect — bCritical=true, bFlushImmediately=true
    CriticalEvent,   // Explicit trigger — game-defined
    ServerShutdown,  // Server shutting down — bCritical=true, bFlushImmediately=true
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

    // Set by UPersistenceSubsystem based on ESerializationReason.
    // Forwarded to IKeyStorageService::Set as call parameters.
    bool bCritical         = false; // Place in DB service priority lane — never dropped
    bool bFlushImmediately = false; // Bypass DB queue, dispatch synchronously

    TArray<FComponentPersistenceBlob> Components;
};
```

---

## Dirty Flag & Generation Counter

The dirty flag is linked to **serialization time**, not save time. When `BuildPayload` runs it captures the current `SaveGeneration`. If a component is dirtied again while its payload is in the DB service's write-behind queue, `DirtyGeneration` advances and the component remains dirty for the next save cycle. The subsystem never needs a "save confirmed" callback from the DB service to maintain correctness.

See `IPersistableComponent` → `ClearIfSaved(uint32 FlushedGeneration)` for the generation counter contract.

---

## Logging

All logging in `UPersistenceSubsystem` uses `FGameCoreBackend::GetLogging()` directly at the call site. No private logging helper methods. If the backend subsystem is not live, `FGameCoreBackend` returns `FNullLoggingService` which routes to `UE_LOG(LogGameCore, ...)` transparently.

```cpp
// Example inline usage throughout the subsystem's .cpp:
#include "Backend/GameCoreBackend.h"

FGameCoreBackend::GetLogging()->LogWarning(TEXT("Persistence"), Message);
FGameCoreBackend::GetLogging()->LogError(TEXT("Persistence"), Message);
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
- `RequestShutdownSave` is synchronous and safe **only** because `RegisteredEntities` is bounded by design. NPCs and Mobs must not register by default — they are respawnable world state. Any new entity category that opts into persistence must have its worst-case registered count evaluated before it is permitted. If the registered set ever grows unbounded, shutdown serialization must be revisited.
- `LoadTimeoutSeconds` should exceed the worst-case DB round-trip. Default 30s is conservative.
- `SaveCounter` is `uint32` and wraps at ~4 billion cycles. At one cycle per 300s this is ~40,000 years.
- `ServerInstanceId` must be set via `DefaultGame.ini` or a `UGameInstance` override before `Initialize()`. If unset, a `UE_LOG Error` is emitted (using `UE_LOG` directly here, before the backend subsystem is guaranteed live).
- Only one transport layer should bind per tag's `OnLoadRequested` — multiple bindings cause duplicate fetch attempts.
- The subsystem has no save queue, no DB flush timer, and no priority queue. All of that is owned by `IKeyStorageService`.

---

## Future Improvements

- Per-tag `ActorsPerFlushTick` budgets — only needed if a non-player entity category is permitted to register and grows large enough to cause starvation
- `CriticalEvent` component key filter on `RequestFullSave`
- PIE teardown guard in `EndPlay`
- Metrics hooks: payload size, flush latency, dropped payload counter
- Load retry logic — currently `OnComplete(false)` fires and game module must handle retry
