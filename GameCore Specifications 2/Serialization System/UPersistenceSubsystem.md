# UPersistenceSubsystem

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Persistence/PersistenceSubsystem.h/.cpp`  
**Type:** `UGameInstanceSubsystem`

Central coordinator of the persistence system. Owns all timing, actor registries, the dirty set, and the load callback map. Produces `FEntityPersistencePayload` blobs and routes them via `FGameplayTag`-keyed delegates. **No knowledge of storage backends** — transport is external.

**Server-only.** `ShouldCreateSubsystem` returns false on clients.

---

## Class Declaration

```cpp
UCLASS(Config=Game)
class GAMECORE_API UPersistenceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // --- Configuration (set via DefaultGame.ini) ---

    /** Seconds between save cycle ticks. Default 300s (5 min). */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    float SaveInterval = 300.f;

    /**
     * Number of partial cycles before a full cycle.
     * 0 = every cycle is Full. 9 = 9 partials then 1 full (default).
     */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    int32 PartialSavesBetweenFullSave = 9;

    /** Actors serialized per game tick. Applies to both partial and full cycles. */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Performance")
    int32 ActorsPerFlushTick = 100;

    /** Seconds before a pending load callback fires OnComplete(false). */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Load")
    float LoadTimeoutSeconds = 30.f;

    /**
     * Stable server identity stamped into every FEntityPersistencePayload.
     * Must be configured before Initialize() via DefaultGame.ini or UGameInstance override.
     * Invalid GUID breaks cross-restart deduplication in audit tooling.
     */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Identity")
    FGuid ServerInstanceId;

    // --- Tag Delegate API ---

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnPayloadReady, const FEntityPersistencePayload&);

    /** Register a tag so the subsystem can route payloads to it. Must be called before BeginPlay of any actor using this tag. */
    void RegisterPersistenceTag(FGameplayTag Tag);

    /** Returns the delegate for a tag. Bind your transport handler here. Returns null if tag not registered. */
    FOnPayloadReady* GetSaveDelegate(FGameplayTag Tag);

    // --- Load API ---

    /**
     * Fired when a load is requested. Game module transport should listen here
     * and fetch from DB, then call OnRawPayloadReceived or OnLoadFailed.
     * Only one transport should bind per tag to avoid duplicate fetch attempts.
     */
    DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLoadRequested, FGuid /*EntityId*/, FGameplayTag /*Tag*/);
    FOnLoadRequested OnLoadRequested;

    /**
     * Request deserialization of an actor from persistent storage.
     * OnComplete fires true on success, false on failure or timeout.
     * Actor must already exist in the world before calling this.
     */
    void RequestLoad(FGuid EntityId, FGameplayTag Tag,
        TFunction<void(bool bSuccess)> OnComplete);

    /**
     * Called by game module transport when DB returns payload data.
     * Deserializes blobs into components, calls Migrate() on version mismatch, fires OnComplete.
     */
    void OnRawPayloadReceived(AActor* Actor, const FEntityPersistencePayload& Payload);

    /** Called by transport on DB fetch failure. Fires OnComplete(false) and removes the pending request. */
    void OnLoadFailed(FGuid EntityId);

    // --- Event-Driven Save API ---

    /**
     * Force an immediate full save for a single actor.
     * Sets bCritical and bFlushImmediately based on ESerializationReason.
     * Removes actor from DirtySet after dispatch.
     */
    void RequestFullSave(AActor* Entity, ESerializationReason Reason);

    /**
     * Synchronous shutdown save. Cancels all timers, serializes all registered entities.
     * All payloads stamped bCritical=true, bFlushImmediately=true.
     * Safe only because RegisteredEntities is bounded by design (no NPCs/Mobs by default).
     */
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

    bool          bFullCycleInProgress = false;
    int32         FullCycleCursorIndex = 0;
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

## Initialize

```cpp
void UPersistenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // ServerInstanceId must be configured before this point.
    // Use UE_LOG directly here — backend subsystem may not be live yet.
    if (!ServerInstanceId.IsValid())
        UE_LOG(LogPersistence, Error,
            TEXT("[Persistence] ServerInstanceId not configured. "
                 "Set via DefaultGame.ini. "
                 "Payloads will be stamped with invalid GUID."));

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
        if (bFullCycleInProgress)
        {
            // Previous full cycle still running — skip this trigger.
            // Avoids double snapshot if SaveInterval is shorter than full cycle completion time.
            return;
        }

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
        if (!RegCompPtr || !RegCompPtr->IsValid()) { continue; }

        FEntityPersistencePayload Payload = RegCompPtr->Get()->BuildPayload(true);
        Payload.PayloadType       = EPayloadType::Full;
        Payload.SaveReason        = ESerializationReason::Periodic;
        Payload.bCritical         = false;
        Payload.bFlushImmediately = false;
        DirtySet.Remove(ID);
        ++Processed;

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
        if (Processed >= ActorsPerFlushTick) break;

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
        ++Processed;

        DispatchPayload(Payload);
    }

    for (const FGuid& ID : ToRemove)
        DirtySet.Remove(ID);
}
```

---

## RequestFullSave — Single Actor Event

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
    if (!Entity) return;

    auto* RegComp = Entity->FindComponentByClass<UPersistenceRegistrationComponent>();
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

## RequestShutdownSave — All Actors, Synchronous

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

> Safe only because `RegisteredEntities` is bounded. NPCs and Mobs must not register by default. If the registered set ever grows unbounded, shutdown serialization must be revisited.

---

## Load Path

```cpp
void UPersistenceSubsystem::RequestLoad(FGuid EntityId, FGameplayTag Tag,
    TFunction<void(bool)> OnComplete)
{
    LoadCallbacks.Add(EntityId,
        { MoveTemp(OnComplete), (float)FPlatformTime::Seconds() });
    OnLoadRequested.Broadcast(EntityId, Tag);
}

void UPersistenceSubsystem::OnRawPayloadReceived(AActor* Actor,
    const FEntityPersistencePayload& Payload)
{
    auto* RegComp = Actor->FindComponentByClass<UPersistenceRegistrationComponent>();
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

## Tag Registration & Dispatch

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
            FString::Printf(TEXT("No delegate for tag [%s]. Payload for entity [%s] dropped."),
                *Payload.PersistenceTag.ToString(),
                *Payload.EntityId.ToString()));
        return;
    }
    Delegate->Broadcast(Payload);
}
```

---

## Logging

All logging uses `FGameCoreBackend::GetLogging()` at the call site. `ServerInstanceId` validation during `Initialize()` uses `UE_LOG` directly because the backend subsystem may not be live yet at that point.

```cpp
#include "Backend/GameCoreBackend.h"

FGameCoreBackend::GetLogging()->LogWarning(TEXT("Persistence"), Message);
FGameCoreBackend::GetLogging()->LogError(TEXT("Persistence"), Message);
```

---

## Notes & Constraints

- `ActorsPerFlushTick` applies to **both** partial and full cycles.
- New actors registering mid-full-cycle are picked up on the next full cycle — acceptable.
- `SaveCounter` wraps at ~4B cycles (~40,000 years at 300s/cycle).
- Only one transport layer should bind `OnLoadRequested` per tag — multiple bindings cause duplicate fetch attempts.
- `RegisteredEntities` must remain bounded. NPCs and Mobs must not register by default.
- If a full cycle is still in progress when the next `FlushSaveCycle` fires, the full cycle trigger is skipped to prevent double snapshots.
