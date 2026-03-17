# UHISMProxyBridgeComponent

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`UHISMProxyBridgeComponent` is the **central coordinator** of the HISM Proxy Actor System. It owns the proxy pool, drives the server-side proximity tick, and manages the full lifecycle of each proxy slot.

When used with `AHISMProxyHostActor`, all configuration is set automatically. When used standalone (advanced), set `TargetHISM`, `Config`, `ProxyClass`, `MinPoolSize`, `MaxPoolSize`, and `GrowthBatchSize` before `BeginPlay`.

**Files:** `HISMProxy/HISMProxyBridgeComponent.h / .cpp`

---

## Delegates

```cpp
// Returns true if InstanceIndex should receive a proxy when a player is nearby.
// If unbound, all instances are treated as eligible.
DECLARE_DELEGATE_RetVal_TwoParams(bool, FHISMInstanceEligibilityDelegate,
    const UHierarchicalInstancedStaticMeshComponent* /*HISM*/,
    int32 /*InstanceIndex*/);
```

---

## Slot State

```cpp
UENUM()
enum class EHISMProxySlotState : uint8
{
    Inactive,      // Proxy is pooled. Actor hidden, collision off.
    Active,        // Proxy is live. HISM instance hidden.
    PendingRemoval // Deactivation timer running. Reverts to Active if player re-enters.
};

// Plain struct — no GENERATED_BODY, not a UPROPERTY.
// GC safety is provided by UHISMProxyBridgeComponent::AllPooledActors
// (a UPROPERTY TArray) which holds the GC-visible reference for every actor.
// FHISMProxySlot::ProxyActor is a raw convenience pointer only.
struct FHISMProxySlot
{
    int32                 InstanceIndex  = INDEX_NONE;
    AHISMProxyActor*      ProxyActor     = nullptr;  // raw ptr — GC root is AllPooledActors
    EHISMProxySlotState   State          = EHISMProxySlotState::Inactive;
    FTimerHandle          DeactivationTimer;
    int32                 PlayerRefCount = 0;
};
```

---

## Class Definition

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UHISMProxyBridgeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHISMProxyBridgeComponent();

    // ── Configuration ─────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TargetHISM;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TObjectPtr<UHISMProxyConfig> Config;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TSubclassOf<AHISMProxyActor> ProxyClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy",
              meta = (ClampMin = "1"))
    int32 MinPoolSize = 8;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy",
              meta = (ClampMin = "0"))
    int32 MaxPoolSize = 64;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy",
              meta = (ClampMin = "1"))
    int32 GrowthBatchSize = 8;

    // ── Delegate Hook ───────────────────────────────────────────────────────

    FHISMInstanceEligibilityDelegate OnQueryInstanceEligibility;

    // ── Game System API ───────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    void NotifyInstanceStateChanged(int32 InstanceIndex);

    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    AHISMProxyActor* GetActiveProxy(int32 InstanceIndex) const;

    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    int32 GetCurrentPoolSize() const { return Slots.Num(); }

    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    int32 GetUsedSlotCount() const { return InstanceToSlotMap.Num(); }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;

private:
    void BuildPool();
    bool GrowPool();
    AHISMProxyActor* SpawnPoolActor();

    void TickProximityCheck();

    void ActivateProxyForInstance(int32 InstanceIndex,
                                   const FTransform& WorldTransform);
    void BeginDeactivation(int32 SlotIdx);
    void OnDeactivationTimerFired(int32 SlotIdx);
    void DeactivateSlotImmediate(int32 SlotIdx);

    void SetHISMInstanceHidden(int32 InstanceIndex, bool bHidden);

    FHISMInstanceSpatialGrid SpatialGrid;

    // Slot metadata. ProxyActor raw ptr is a convenience accessor;
    // GC ownership lives in AllPooledActors.
    TArray<FHISMProxySlot> Slots;

    // GC root for all pooled actors. Every spawned actor is added here
    // and cleared only in EndPlay.
    UPROPERTY()
    TArray<TObjectPtr<AHISMProxyActor>> AllPooledActors;

    // Instance index → slot index. Active and PendingRemoval entries only.
    TMap<int32, int32> InstanceToSlotMap;

    // Free list: indices into Slots[] that are currently Inactive.
    TArray<int32> FreeSlotIndices;

    // Pre-allocated scratch buffers for TickProximityCheck.
    // Declared as members to avoid per-tick heap allocation.
    TMap<int32, int32> TickInstancePlayerCount;
    TArray<FVector>    TickPlayerPositions;
    TArray<int32>      TickCandidates;
    TArray<int32>      TickSlotsToDeactivate;
    TArray<int32>      TickSlotsToRevive;

    FTimerHandle ProximityTickHandle;

    // Set in BeginPlay before BuildPool. Pool actors spawn here:
    // near host actor XY, 1km below terrain Z.
    FVector PoolSpawnLocation = FVector::ZeroVector;
};
```

---

## Implementation — `BeginPlay`

```cpp
void UHISMProxyBridgeComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!GetOwner()->HasAuthority()) { return; }

    if (!TargetHISM || !Config || !ProxyClass)
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UHISMProxyBridgeComponent [%s]: TargetHISM, Config, or ProxyClass is null — disabled."),
            *GetName());
        return;
    }

    // PoolSpawnLocation MUST be set before BuildPool is called.
    // Actors here are in the same streaming cell as the host but 1km below terrain.
    PoolSpawnLocation = GetOwner()->GetActorLocation() + FVector(0.f, 0.f, -100000.f);

    SpatialGrid.Build(TargetHISM, Config->GridCellSize);
    BuildPool();

    GetWorld()->GetTimerManager().SetTimer(
        ProximityTickHandle, this,
        &UHISMProxyBridgeComponent::TickProximityCheck,
        Config->ProximityTickInterval, /*bLoop=*/true);
}
```

---

## Implementation — `BuildPool` and `GrowPool`

```cpp
AHISMProxyActor* UHISMProxyBridgeComponent::SpawnPoolActor()
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AHISMProxyActor* Actor = GetWorld()->SpawnActor<AHISMProxyActor>(
        ProxyClass, FTransform(PoolSpawnLocation), Params);

    if (!Actor)
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UHISMProxyBridgeComponent [%s]: SpawnActor failed for class %s."),
            *GetName(), *ProxyClass->GetName());
        return nullptr;
    }

    Actor->SetActorHiddenInGame(true);
    Actor->SetActorEnableCollision(false);
    AllPooledActors.Add(Actor); // GC root
    return Actor;
}

void UHISMProxyBridgeComponent::BuildPool()
{
    Slots.Reserve(MinPoolSize);
    FreeSlotIndices.Reserve(MinPoolSize);
    AllPooledActors.Reserve(MinPoolSize);

    for (int32 i = 0; i < MinPoolSize; ++i)
    {
        AHISMProxyActor* Actor = SpawnPoolActor();
        if (!Actor) { break; }

        FHISMProxySlot& Slot = Slots.AddDefaulted_GetRef();
        Slot.ProxyActor = Actor;
        FreeSlotIndices.Add(i);
    }
}

bool UHISMProxyBridgeComponent::GrowPool()
{
    const int32 CurrentSize = Slots.Num();

    if (MaxPoolSize > 0 && CurrentSize >= MaxPoolSize)
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UHISMProxyBridgeComponent [%s]: MaxPoolSize (%d) reached. "
                 "Increase MinPoolSize."),
            *GetName(), MaxPoolSize);
        return false;
    }

    UE_LOG(LogGameCore, Warning,
        TEXT("UHISMProxyBridgeComponent [%s]: pool exhausted (%d used). "
             "Growing by %d. Increase MinPoolSize to eliminate runtime spawning."),
        *GetName(), CurrentSize, GrowthBatchSize);

    const int32 ToAdd = (MaxPoolSize > 0)
        ? FMath::Min(GrowthBatchSize, MaxPoolSize - CurrentSize)
        : GrowthBatchSize;

    for (int32 i = 0; i < ToAdd; ++i)
    {
        AHISMProxyActor* Actor = SpawnPoolActor();
        if (!Actor) { break; }

        const int32 NewSlotIdx = Slots.Num();
        FHISMProxySlot& Slot = Slots.AddDefaulted_GetRef();
        Slot.ProxyActor = Actor;
        FreeSlotIndices.Add(NewSlotIdx);
    }

    return !FreeSlotIndices.IsEmpty();
}
```

---

## Implementation — `GetActiveProxy`

```cpp
AHISMProxyActor* UHISMProxyBridgeComponent::GetActiveProxy(int32 InstanceIndex) const
{
    if (const int32* SlotIdx = InstanceToSlotMap.Find(InstanceIndex))
    {
        const FHISMProxySlot& Slot = Slots[*SlotIdx];
        if (Slot.State == EHISMProxySlotState::Active ||
            Slot.State == EHISMProxySlotState::PendingRemoval)
        {
            return Slot.ProxyActor;
        }
    }
    return nullptr;
}
```

---

## Implementation — `TickProximityCheck`

Scratch buffers (`TickInstancePlayerCount`, `TickPlayerPositions`, etc.) are **member arrays** cleared at tick start. This eliminates per-tick heap allocation from local `TMap`/`TArray` construction, which at 0.5s intervals across many bridge components would produce significant allocator pressure.

Deferred deactivation: `InstanceToSlotMap` is never modified while being iterated. State changes are collected and applied after the iteration loop.

`SlotIdx` is captured by value in the timer delegate. This is safe because `Slots[]` only grows — a given index always addresses the same slot for the component lifetime.

```cpp
void UHISMProxyBridgeComponent::TickProximityCheck()
{
    if (!SpatialGrid.IsBuilt()) { return; }

    const float ActRadiusSq = FMath::Square(Config->ActivationRadius);

    // 1. Gather player positions — reuse member buffer.
    TickPlayerPositions.Reset();
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
         It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (PC && PC->GetPawn())
            TickPlayerPositions.Add(PC->GetPawn()->GetActorLocation());
    }
    if (TickPlayerPositions.IsEmpty()) { return; }

    // 2. Spatial grid query — reuse member map and candidate buffer.
    TickInstancePlayerCount.Reset();
    for (const FVector& PlayerPos : TickPlayerPositions)
    {
        TickCandidates.Reset();
        SpatialGrid.QueryRadius(PlayerPos, Config->ActivationRadius, TickCandidates);
        for (int32 Idx : TickCandidates)
        {
            if (FVector::DistSquared(PlayerPos, SpatialGrid.GetInstancePosition(Idx))
                    <= ActRadiusSq)
            {
                TickInstancePlayerCount.FindOrAdd(Idx)++;
            }
        }
    }

    // 3. Evaluate managed slots — deferred: collect, do not modify map here.
    TickSlotsToDeactivate.Reset();
    TickSlotsToRevive.Reset();

    for (const auto& [InstanceIdx, SlotIdx] : InstanceToSlotMap)
    {
        FHISMProxySlot& Slot = Slots[SlotIdx];
        Slot.PlayerRefCount = TickInstancePlayerCount.FindRef(InstanceIdx);

        if (Slot.PlayerRefCount > 0 &&
            Slot.State == EHISMProxySlotState::PendingRemoval)
        {
            TickSlotsToRevive.Add(SlotIdx);
        }
        else if (Slot.PlayerRefCount == 0 &&
                 Slot.State == EHISMProxySlotState::Active)
        {
            TickSlotsToDeactivate.Add(SlotIdx);
        }
    }

    // 4. Apply deferred state changes — safe to modify map now.
    for (int32 SlotIdx : TickSlotsToRevive)
    {
        GetWorld()->GetTimerManager().ClearTimer(Slots[SlotIdx].DeactivationTimer);
        Slots[SlotIdx].State = EHISMProxySlotState::Active;
    }
    for (int32 SlotIdx : TickSlotsToDeactivate)
    {
        BeginDeactivation(SlotIdx);
    }

    // 5. Activate proxies for newly in-range instances.
    for (const auto& [InstanceIdx, Count] : TickInstancePlayerCount)
    {
        if (InstanceToSlotMap.Contains(InstanceIdx)) { continue; }

        if (OnQueryInstanceEligibility.IsBound() &&
            !OnQueryInstanceEligibility.Execute(TargetHISM, InstanceIdx))
        {
            continue;
        }

        FTransform T;
        TargetHISM->GetInstanceTransform(InstanceIdx, T, /*bWorldSpace=*/true);
        ActivateProxyForInstance(InstanceIdx, T);
    }
}
```

---

## Implementation — Activation / Deactivation

```cpp
void UHISMProxyBridgeComponent::ActivateProxyForInstance(
    int32 InstanceIndex, const FTransform& WorldTransform)
{
    if (FreeSlotIndices.IsEmpty())
    {
        if (!GrowPool()) { return; }
    }

    const int32 SlotIdx = FreeSlotIndices.Pop(/*bAllowShrinking=*/false);
    FHISMProxySlot& Slot = Slots[SlotIdx];

    Slot.InstanceIndex  = InstanceIndex;
    Slot.State          = EHISMProxySlotState::Active;
    Slot.PlayerRefCount = 1;

    InstanceToSlotMap.Add(InstanceIndex, SlotIdx);

    Slot.ProxyActor->SetActorTransform(WorldTransform);
    Slot.ProxyActor->SetActorHiddenInGame(false);
    Slot.ProxyActor->SetActorEnableCollision(true);

    SetHISMInstanceHidden(InstanceIndex, true);
    Slot.ProxyActor->OnProxyActivated(InstanceIndex, WorldTransform);
}

void UHISMProxyBridgeComponent::BeginDeactivation(int32 SlotIdx)
{
    Slots[SlotIdx].State = EHISMProxySlotState::PendingRemoval;

    // SlotIdx captured by value. Safe: Slots[] only grows, index is stable
    // for the component lifetime.
    FTimerDelegate D;
    D.BindUObject(this, &UHISMProxyBridgeComponent::OnDeactivationTimerFired, SlotIdx);
    GetWorld()->GetTimerManager().SetTimer(
        Slots[SlotIdx].DeactivationTimer, D,
        Config->DeactivationDelay, /*bLoop=*/false);
}

void UHISMProxyBridgeComponent::OnDeactivationTimerFired(int32 SlotIdx)
{
    DeactivateSlotImmediate(SlotIdx);
}

void UHISMProxyBridgeComponent::DeactivateSlotImmediate(int32 SlotIdx)
{
    FHISMProxySlot& Slot = Slots[SlotIdx];
    if (Slot.State == EHISMProxySlotState::Inactive) { return; }

    GetWorld()->GetTimerManager().ClearTimer(Slot.DeactivationTimer);

    Slot.ProxyActor->OnProxyDeactivated();
    Slot.ProxyActor->SetActorHiddenInGame(true);
    Slot.ProxyActor->SetActorEnableCollision(false);
    Slot.ProxyActor->SetActorLocation(PoolSpawnLocation);

    SetHISMInstanceHidden(Slot.InstanceIndex, false);

    InstanceToSlotMap.Remove(Slot.InstanceIndex);
    Slot.InstanceIndex  = INDEX_NONE;
    Slot.State          = EHISMProxySlotState::Inactive;
    Slot.PlayerRefCount = 0;

    FreeSlotIndices.Add(SlotIdx);
}

void UHISMProxyBridgeComponent::SetHISMInstanceHidden(
    int32 InstanceIndex, bool bHidden)
{
    TargetHISM->SetCustomDataValue(
        InstanceIndex, /*CustomDataIndex=*/0,
        bHidden ? 1.f : 0.f, /*bMarkRenderStateDirty=*/true);
}
```

---

## `EndPlay`

```cpp
void UHISMProxyBridgeComponent::EndPlay(EEndPlayReason::Type Reason)
{
    GetWorld()->GetTimerManager().ClearTimer(ProximityTickHandle);

    // Iterate Slots[] directly — not InstanceToSlotMap — so
    // DeactivateSlotImmediate can modify the map without invalidating this loop.
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        if (Slots[i].State != EHISMProxySlotState::Inactive)
            DeactivateSlotImmediate(i);
    }

    AllPooledActors.Empty();
    Super::EndPlay(Reason);
}
```

---

## `NotifyInstanceStateChanged`

```cpp
void UHISMProxyBridgeComponent::NotifyInstanceStateChanged(int32 InstanceIndex)
{
    if (!GetOwner()->HasAuthority()) { return; }
    if (const int32* SlotIdx = InstanceToSlotMap.Find(InstanceIndex))
        DeactivateSlotImmediate(*SlotIdx);
}
```

Safe to call from within `OnProxyActivated` — `ActivateProxyForInstance` runs after all `InstanceToSlotMap` iteration is complete within `TickProximityCheck`.

---

## Notes

- **GC safety via `AllPooledActors`.** `FHISMProxySlot::ProxyActor` is a raw pointer. The GC root is `AllPooledActors` (a `UPROPERTY TArray`). Both point to the same actor; only `AllPooledActors` keeps it alive.
- **No per-tick heap allocation.** `TickInstancePlayerCount`, `TickPlayerPositions`, `TickCandidates`, `TickSlotsToDeactivate`, `TickSlotsToRevive` are member arrays. They are `Reset()` at tick start (clears count, retains capacity) — no allocator involvement after first tick.
- **`PoolSpawnLocation` set before `BuildPool`.** Ordering enforced in `BeginPlay`. Do not call `BuildPool` from subclass constructors.
- **`Slots[]` only grows; indices are stable.** Timer delegate captures `SlotIdx` by value safely.
- **`EndPlay` iterates `Slots[]` not `InstanceToSlotMap`.** Map mutation inside `DeactivateSlotImmediate` does not invalidate the loop.
- **Pool growth is a warning-level event.** Raise `MinPoolSize` if it appears in production logs.
