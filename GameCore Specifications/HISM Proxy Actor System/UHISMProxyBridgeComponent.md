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

struct FHISMProxySlot
{
    int32                       InstanceIndex  = INDEX_NONE;
    TObjectPtr<AHISMProxyActor> ProxyActor     = nullptr;
    EHISMProxySlotState         State          = EHISMProxySlotState::Inactive;
    FTimerHandle                DeactivationTimer;
    int32                       PlayerRefCount = 0;
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
    // Set automatically by AHISMProxyHostActor. Set manually for standalone use.

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TargetHISM;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TObjectPtr<UHISMProxyConfig> Config;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TSubclassOf<AHISMProxyActor> ProxyClass;

    // Pre-allocated pool size at BeginPlay.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy",
              meta = (ClampMin = "1"))
    int32 MinPoolSize = 8;

    // Hard ceiling on pool growth. 0 = strict pre-allocation (no growth).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy",
              meta = (ClampMin = "0"))
    int32 MaxPoolSize = 64;

    // Actors spawned per growth step when pool is exhausted.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy",
              meta = (ClampMin = "1"))
    int32 GrowthBatchSize = 8;

    // ── Delegate Hook ─────────────────────────────────────────────────────────

    FHISMInstanceEligibilityDelegate OnQueryInstanceEligibility;

    // ── Game System API ───────────────────────────────────────────────────────

    // Forces immediate deactivation of the proxy for this instance.
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    void NotifyInstanceStateChanged(int32 InstanceIndex);

    // Returns the live proxy for this instance, or nullptr.
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    AHISMProxyActor* GetActiveProxy(int32 InstanceIndex) const;

    // Current pool size (grows beyond MinPoolSize if exhausted).
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    int32 GetCurrentPoolSize() const { return Slots.Num(); }

    // Slots currently occupied (Active + PendingRemoval).
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    int32 GetUsedSlotCount() const { return InstanceToSlotMap.Num(); }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;

    // Override in C++ subclasses for heterogeneous HISM (advanced use).
    virtual TSubclassOf<AHISMProxyActor> ResolveProxyClass(int32 InstanceIndex) const;

private:
    // Spawns MinPoolSize actors and fills FreeSlotIndices.
    void BuildPool();

    // Grows pool by GrowthBatchSize, up to MaxPoolSize.
    // Returns true if at least one slot was added.
    bool GrowPool();

    // Spawns one pool actor at a safe location (near host, far below terrain).
    AHISMProxyActor* SpawnPoolActor();

    void TickProximityCheck();

    void ActivateProxyForInstance(int32 InstanceIndex,
                                   const FTransform& WorldTransform);
    void BeginDeactivation(int32 SlotIdx);
    void OnDeactivationTimerFired(int32 SlotIdx);
    void DeactivateSlotImmediate(int32 SlotIdx);

    void SetHISMInstanceHidden(int32 InstanceIndex, bool bHidden);

    FHISMInstanceSpatialGrid SpatialGrid;

    // All allocated slots (Inactive + Active + PendingRemoval).
    TArray<FHISMProxySlot> Slots;

    // Instance index → slot index. Contains Active and PendingRemoval entries only.
    TMap<int32, int32> InstanceToSlotMap;

    // Flat free list: indices into Slots[] that are Inactive.
    TArray<int32> FreeSlotIndices;

    FTimerHandle ProximityTickHandle;

    // Cached safe spawn location (near host actor, Z far below terrain).
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

    // Safe location for pool actors: near the host actor but 1km below terrain.
    // This keeps them in the same streaming cell as the host while preventing
    // navmesh impact and visibility during PIE startup.
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

    // Spawn at the precomputed safe location: near host, 1km below terrain.
    // Actors here do not affect navmesh generation or collision queries.
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
    return Actor;
}

void UHISMProxyBridgeComponent::BuildPool()
{
    Slots.Reserve(MinPoolSize);
    FreeSlotIndices.Reserve(MinPoolSize);

    for (int32 i = 0; i < MinPoolSize; ++i)
    {
        AHISMProxyActor* Actor = SpawnPoolActor();
        if (!Actor) { break; } // SpawnActor logged the error already

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
                 "Increase MinPoolSize to avoid runtime spawning."),
            *GetName(), MaxPoolSize);
        return false;
    }

    // Log a warning on every growth event — this should never happen in production
    // if MinPoolSize is correctly sized.
    UE_LOG(LogGameCore, Warning,
        TEXT("UHISMProxyBridgeComponent [%s]: pool exhausted (%d slots used). "
             "Growing by %d. Consider increasing MinPoolSize."),
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

The critical fix here is the **deferred deactivation list**: instead of calling `BeginDeactivation` while iterating `InstanceToSlotMap`, we collect slot indices to deactivate and process them after the iteration completes. This prevents any possibility of map modification during iteration.

```cpp
void UHISMProxyBridgeComponent::TickProximityCheck()
{
    if (!SpatialGrid.IsBuilt()) { return; }

    const float ActRadiusSq = FMath::Square(Config->ActivationRadius);

    // 1. Gather all player pawn positions.
    TArray<FVector> PlayerPositions;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
         It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (PC && PC->GetPawn())
            PlayerPositions.Add(PC->GetPawn()->GetActorLocation());
    }
    if (PlayerPositions.IsEmpty()) { return; }

    // 2. Query spatial grid for all in-range instances across all players.
    TMap<int32, int32> InstancePlayerCount;
    for (const FVector& PlayerPos : PlayerPositions)
    {
        TArray<int32> Candidates;
        SpatialGrid.QueryRadius(PlayerPos, Config->ActivationRadius, Candidates);
        for (int32 Idx : Candidates)
        {
            if (FVector::DistSquared(PlayerPos, SpatialGrid.GetInstancePosition(Idx))
                    <= ActRadiusSq)
            {
                InstancePlayerCount.FindOrAdd(Idx)++;
            }
        }
    }

    // 3. Evaluate already-managed slots.
    // IMPORTANT: Do NOT call DeactivateSlotImmediate here — it modifies
    // InstanceToSlotMap which we are currently iterating. Use deferred lists.
    TArray<int32> SlotsToBeginDeactivation;
    TArray<int32> SlotsToRevive; // PendingRemoval slots where players returned

    for (const auto& [InstanceIdx, SlotIdx] : InstanceToSlotMap)
    {
        FHISMProxySlot& Slot = Slots[SlotIdx];
        Slot.PlayerRefCount = InstancePlayerCount.FindRef(InstanceIdx);

        if (Slot.PlayerRefCount > 0 &&
            Slot.State == EHISMProxySlotState::PendingRemoval)
        {
            SlotsToRevive.Add(SlotIdx);
        }
        else if (Slot.PlayerRefCount == 0 &&
                 Slot.State == EHISMProxySlotState::Active)
        {
            SlotsToBeginDeactivation.Add(SlotIdx);
        }
    }

    // 4. Apply deferred state changes — safe to modify map now.
    for (int32 SlotIdx : SlotsToRevive)
    {
        GetWorld()->GetTimerManager().ClearTimer(Slots[SlotIdx].DeactivationTimer);
        Slots[SlotIdx].State = EHISMProxySlotState::Active;
    }
    for (int32 SlotIdx : SlotsToBeginDeactivation)
    {
        BeginDeactivation(SlotIdx);
    }

    // 5. Activate proxies for newly in-range instances.
    for (const auto& [InstanceIdx, Count] : InstancePlayerCount)
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

## Implementation — Activation

```cpp
void UHISMProxyBridgeComponent::ActivateProxyForInstance(
    int32 InstanceIndex, const FTransform& WorldTransform)
{
    // Attempt to acquire a free slot; grow if necessary.
    if (FreeSlotIndices.IsEmpty())
    {
        if (!GrowPool())
        {
            // GrowPool logged the error (MaxPoolSize hit or spawn failed).
            return;
        }
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
```

---

## Implementation — Deactivation

```cpp
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
    // Timer callback fires on game thread — safe to call directly.
    DeactivateSlotImmediate(SlotIdx);
}

void UHISMProxyBridgeComponent::DeactivateSlotImmediate(int32 SlotIdx)
{
    FHISMProxySlot& Slot = Slots[SlotIdx];
    if (Slot.State == EHISMProxySlotState::Inactive) { return; }

    // Always clear timer first — safe even if the timer is not active.
    GetWorld()->GetTimerManager().ClearTimer(Slot.DeactivationTimer);

    Slot.ProxyActor->OnProxyDeactivated();
    Slot.ProxyActor->SetActorHiddenInGame(true);
    Slot.ProxyActor->SetActorEnableCollision(false);
    // Move back to safe pool location — prevents being visible through level bounds.
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

    // Deactivate all slots — restores HISM instance visibility before teardown.
    // Iterate by index (not the map) so DeactivateSlotImmediate can safely
    // modify InstanceToSlotMap without affecting our loop.
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        if (Slots[i].State != EHISMProxySlotState::Inactive)
            DeactivateSlotImmediate(i);
    }

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

**Note:** It is safe to call `NotifyInstanceStateChanged` at any time from game systems, including from within `OnProxyActivated`. It does **not** modify `InstanceToSlotMap` during `TickProximityCheck`'s iteration because `ActivateProxyForInstance` (step 5 of the tick) is called after all managed-slot iteration is complete.

---

## Notes

- **Deferred deactivation list** in `TickProximityCheck` (Critical Issue #4 fix): `InstanceToSlotMap` is never modified while being iterated. All deactivations are collected into `SlotsToBeginDeactivation` and applied after the loop.
- **Pool spawn location** is computed once in `BeginPlay` as `HostActorLocation + (0,0,-100000)`. Pool actors sit 1km below terrain — in the same streaming cell, but invisible and outside navmesh influence.
- **Growth is a safety net.** Every growth event logs a `Warning`. Monitor this in production and raise `MinPoolSize` to eliminate it.
- **No pool shrinking.** Pool size is monotonically non-decreasing for the lifetime of the world. Memory is reclaimed on server restart.
- **`EndPlay` iterates `Slots[]` directly**, not `InstanceToSlotMap`, so `DeactivateSlotImmediate`'s map removal does not invalidate the loop.
