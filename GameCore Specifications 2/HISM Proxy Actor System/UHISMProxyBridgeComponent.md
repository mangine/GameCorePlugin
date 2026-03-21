# UHISMProxyBridgeComponent

**File:** `GameCore/Source/GameCore/Public/HISMProxy/HISMProxyBridgeComponent.h / .cpp`  
**Module:** `GameCore`

The central coordinator. Owns the proxy pool, drives the server-side proximity tick, and manages the full lifecycle of each proxy slot. One bridge manages exactly one HISM component.

---

## Supporting Types

```cpp
// Returns true if InstanceIndex should receive a proxy when a player is nearby.
// If unbound, all instances are treated as eligible.
DECLARE_DELEGATE_RetVal_TwoParams(bool, FHISMInstanceEligibilityDelegate,
    const UHierarchicalInstancedStaticMeshComponent* /*HISM*/,
    int32 /*InstanceIndex*/);

UENUM()
enum class EHISMProxySlotState : uint8
{
    Inactive,      // Pooled. Actor hidden, collision off.
    Active,        // Live. HISM instance hidden by CustomData.
    PendingRemoval // Deactivation timer running. Reverts to Active if player re-enters.
};

// Plain struct — no GENERATED_BODY, not a UPROPERTY.
// GC safety is provided by UHISMProxyBridgeComponent::AllPooledActors
// (a UPROPERTY TArray). FHISMProxySlot::ProxyActor is a raw convenience pointer only.
struct FHISMProxySlot
{
    int32               InstanceIndex  = INDEX_NONE;
    AHISMProxyActor*    ProxyActor     = nullptr;  // raw ptr — GC root is AllPooledActors
    EHISMProxySlotState State          = EHISMProxySlotState::Inactive;
    FTimerHandle        DeactivationTimer;
    int32               PlayerRefCount = 0;
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

    // ── Configuration ────────────────────────────────────────────────────────

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

    // ── Delegate Hook ────────────────────────────────────────────────────────

    // Bind from a game subsystem to veto proxy activation for specific instances.
    // Called on the server only. If unbound, all instances are eligible.
    FHISMInstanceEligibilityDelegate OnQueryInstanceEligibility;

    // ── Game System API ───────────────────────────────────────────────────────

    // Force-deactivates the active proxy for InstanceIndex (if any).
    // Use when an instance changes to a state that should suppress proxy activation
    // (e.g. resource depleted). The eligibility delegate will block re-activation.
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    void NotifyInstanceStateChanged(int32 InstanceIndex);

    // Returns the active or PendingRemoval proxy for InstanceIndex, or nullptr.
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

    void ActivateProxyForInstance(int32 InstanceIndex, const FTransform& WorldTransform);
    void BeginDeactivation(int32 SlotIdx);
    void OnDeactivationTimerFired(int32 SlotIdx);
    void DeactivateSlotImmediate(int32 SlotIdx);

    void SetHISMInstanceHidden(int32 InstanceIndex, bool bHidden);

    FHISMInstanceSpatialGrid SpatialGrid;

    // Slot state. ProxyActor raw ptr is a convenience accessor;
    // GC ownership lives in AllPooledActors.
    TArray<FHISMProxySlot> Slots;

    // GC root for all pooled actors. Added in SpawnPoolActor, cleared in EndPlay.
    UPROPERTY()
    TArray<TObjectPtr<AHISMProxyActor>> AllPooledActors;

    // Instance index → slot index. Active and PendingRemoval entries only.
    TMap<int32, int32> InstanceToSlotMap;

    // Free list: indices into Slots[] that are currently Inactive.
    TArray<int32> FreeSlotIndices;

    // Pre-allocated scratch buffers — member arrays to avoid per-tick heap allocation.
    // See AD-13 in Architecture.md.
    TMap<int32, int32> TickInstancePlayerCount;
    TArray<FVector>    TickPlayerPositions;
    TArray<int32>      TickCandidates;
    TArray<int32>      TickSlotsToDeactivate;
    TArray<int32>      TickSlotsToRevive;

    FTimerHandle ProximityTickHandle;

    // Set in BeginPlay before BuildPool. Pool actors spawn at host XY, -1km Z.
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

    // Must be set before BuildPool — pool actors spawn here.
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

## Implementation — Pool Management

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
            TEXT("UHISMProxyBridgeComponent [%s]: MaxPoolSize (%d) reached. Increase MinPoolSize."),
            *GetName(), MaxPoolSize);
        return false;
    }

    UE_LOG(LogGameCore, Warning,
        TEXT("UHISMProxyBridgeComponent [%s]: pool exhausted (%d used). Growing by %d. "
             "Increase MinPoolSize to eliminate runtime spawning."),
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

## Implementation — `TickProximityCheck`

Scratch buffers (`TickInstancePlayerCount` etc.) are member arrays cleared with `Reset()` at tick start — no heap allocation after the first tick (AD-13).

Deferred state changes: `InstanceToSlotMap` is never modified while being iterated — changes are collected and applied after the loop.

`SlotIdx` captured by value in timer delegates is safe because `Slots[]` only grows — a given index always addresses the same slot for the component lifetime.

```cpp
void UHISMProxyBridgeComponent::TickProximityCheck()
{
    if (!SpatialGrid.IsBuilt()) { return; }

    const float ActRadiusSq = FMath::Square(Config->ActivationRadius);

    // 1. Gather all player positions.
    TickPlayerPositions.Reset();
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
         It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (PC && PC->GetPawn())
            TickPlayerPositions.Add(PC->GetPawn()->GetActorLocation());
    }
    if (TickPlayerPositions.IsEmpty()) { return; }

    // 2. Spatial grid query — reuse scratch map and candidate buffer.
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

    // 3. Evaluate managed slots — collect deferred changes.
    TickSlotsToDeactivate.Reset();
    TickSlotsToRevive.Reset();

    for (const auto& [InstanceIdx, SlotIdx] : InstanceToSlotMap)
    {
        FHISMProxySlot& Slot = Slots[SlotIdx];
        Slot.PlayerRefCount = TickInstancePlayerCount.FindRef(InstanceIdx);

        if (Slot.PlayerRefCount > 0 && Slot.State == EHISMProxySlotState::PendingRemoval)
            TickSlotsToRevive.Add(SlotIdx);
        else if (Slot.PlayerRefCount == 0 && Slot.State == EHISMProxySlotState::Active)
            TickSlotsToDeactivate.Add(SlotIdx);
    }

    // 4. Apply deferred state changes — map is safe to modify now.
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

## Implementation — `GetActiveProxy` and `NotifyInstanceStateChanged`

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

void UHISMProxyBridgeComponent::NotifyInstanceStateChanged(int32 InstanceIndex)
{
    if (!GetOwner()->HasAuthority()) { return; }
    if (const int32* SlotIdx = InstanceToSlotMap.Find(InstanceIndex))
        DeactivateSlotImmediate(*SlotIdx);
}
```

---

## Implementation — `EndPlay`

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

## Notes

- **GC safety via `AllPooledActors`.** `FHISMProxySlot::ProxyActor` is a raw pointer for performance. The GC root is `AllPooledActors` (`UPROPERTY TArray`). Both point to the same actor.
- **No per-tick heap allocation.** Scratch buffers are `Reset()` at tick start — retains capacity, no allocator involvement after first tick.
- **`PoolSpawnLocation` must be set before `BuildPool`.** Ordering enforced in `BeginPlay`.
- **`Slots[]` only grows; indices are stable.** Timer delegates capturing `SlotIdx` by value are safe for the component lifetime.
- **`EndPlay` iterates `Slots[]` not `InstanceToSlotMap`.** Map mutation inside `DeactivateSlotImmediate` does not invalidate the loop.
- **`NotifyInstanceStateChanged` is safe from `OnProxyActivated`.** `ActivateProxyForInstance` runs after all `InstanceToSlotMap` iteration is complete.
