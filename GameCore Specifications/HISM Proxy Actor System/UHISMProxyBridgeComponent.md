# UHISMProxyBridgeComponent

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`UHISMProxyBridgeComponent` is the **central coordinator** of the HISM Proxy Actor System. It lives on the HISM host Actor, owns the proxy pool, drives the server-side proximity tick, and manages the full lifecycle of each proxy slot.

When used with `AHISMProxyHostActor`, the host actor creates and wires bridge components automatically. When used standalone (advanced), the developer must set `TargetHISM`, `Config`, `PoolSize`, and `ProxyClass` manually.

**Files:** `HISMProxy/HISMProxyBridgeComponent.h / .cpp`

---

## Delegates

```cpp
// Returns true if instance InstanceIndex should be eligible for a proxy
// when a player is nearby. If unbound, all instances are treated as eligible.
// Bind from your game system to filter instances that are on cooldown,
// depleted, or otherwise inert.
DECLARE_DELEGATE_RetVal_TwoParams(bool, FHISMInstanceEligibilityDelegate,
    const UHierarchicalInstancedStaticMeshComponent* /*HISM*/,
    int32 /*InstanceIndex*/);
```

> **Note:** `FHISMInstanceTypeDelegate` has been removed. When managed by `AHISMProxyHostActor`, each bridge component manages a single homogeneous HISM — all instances use the same proxy class, so type dispatch is unnecessary. For heterogeneous cases (advanced, standalone use), subclass the bridge and override `ResolveProxyClass`.

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
    int32                InstanceIndex  = INDEX_NONE;
    TObjectPtr<AHISMProxyActor> ProxyActor = nullptr;
    EHISMProxySlotState  State          = EHISMProxySlotState::Inactive;
    FTimerHandle         DeactivationTimer;
    int32                PlayerRefCount = 0;
    FGameplayTag         TypeTag;          // cached at activation to avoid re-query on deactivation
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
    // When managed by AHISMProxyHostActor, all of these are set automatically.
    // When used standalone, set these before BeginPlay.

    // The HISM component this bridge manages. Explicit reference — no FindComponentByClass.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TargetHISM;

    // Proximity and timing config.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TObjectPtr<UHISMProxyConfig> Config;

    // The Blueprint subclass of AHISMProxyActor to pool and activate.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TSubclassOf<AHISMProxyActor> ProxyClass;

    // Max concurrent live proxies from this bridge's pool.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy",
              meta = (ClampMin = "1"))
    int32 PoolSize = 8;

    // ── External Delegate Hook ────────────────────────────────────────────────

    // Optionally bind from a game system to filter ineligible instances.
    // If unbound, all instances receive proxies when in range.
    FHISMInstanceEligibilityDelegate OnQueryInstanceEligibility;

    // ── Game System API ───────────────────────────────────────────────────────

    // Forces immediate deactivation of the proxy for the given instance index,
    // regardless of player proximity. Use when instance state changes externally
    // (e.g. resource harvested, on cooldown, destroyed).
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    void NotifyInstanceStateChanged(int32 InstanceIndex);

    // Returns the active proxy actor for the given instance index.
    // Returns nullptr if the instance has no active proxy.
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    AHISMProxyActor* GetActiveProxy(int32 InstanceIndex) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;

    // Override in subclasses for heterogeneous HISM (advanced / standalone use).
    // Default implementation returns the single configured ProxyClass.
    virtual TSubclassOf<AHISMProxyActor> ResolveProxyClass(int32 InstanceIndex) const;

private:
    void BuildPool();
    AHISMProxyActor* AcquireFromPool();
    void ReturnToPool(int32 SlotIdx);

    void TickProximityCheck();

    void ActivateProxyForInstance(int32 InstanceIndex,
                                   const FTransform& InstanceWorldTransform);
    void BeginDeactivation(int32 SlotIdx);
    void OnDeactivationTimerFired(int32 SlotIdx);
    void DeactivateSlotImmediate(int32 SlotIdx);

    void SetHISMInstanceHidden(int32 InstanceIndex, bool bHidden);

    // Spatial grid built at BeginPlay from TargetHISM.
    FHISMInstanceSpatialGrid SpatialGrid;

    // All slots — size equals PoolSize.
    TArray<FHISMProxySlot> Slots;

    // Instance index → slot index. Active and PendingRemoval entries only.
    TMap<int32, int32> InstanceToSlotMap;

    // Indices into Slots[] that are currently Inactive (free list).
    TArray<int32> FreeSlotIndices;

    FTimerHandle ProximityTickHandle;
};
```

---

## Implementation — `BeginPlay`

```cpp
void UHISMProxyBridgeComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!GetOwner()->HasAuthority()) { return; }

    if (!TargetHISM)
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UHISMProxyBridgeComponent [%s]: TargetHISM is null — disabled."),
            *GetName());
        return;
    }
    if (!Config || !ProxyClass)
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UHISMProxyBridgeComponent [%s]: Config or ProxyClass is null — disabled."),
            *GetName());
        return;
    }

    SpatialGrid.Build(TargetHISM, Config->GridCellSize);
    BuildPool();

    GetWorld()->GetTimerManager().SetTimer(
        ProximityTickHandle, this,
        &UHISMProxyBridgeComponent::TickProximityCheck,
        Config->ProximityTickInterval, /*bLoop=*/true);
}
```

---

## Implementation — `BuildPool`

```cpp
void UHISMProxyBridgeComponent::BuildPool()
{
    Slots.Reserve(PoolSize);
    FreeSlotIndices.Reserve(PoolSize);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    for (int32 i = 0; i < PoolSize; ++i)
    {
        AHISMProxyActor* Actor = GetWorld()->SpawnActor<AHISMProxyActor>(
            ProxyClass, FTransform::Identity, Params);
        check(Actor);
        Actor->SetActorHiddenInGame(true);
        Actor->SetActorEnableCollision(false);

        FHISMProxySlot& Slot = Slots.AddDefaulted_GetRef();
        Slot.ProxyActor = Actor;
        FreeSlotIndices.Add(i);
    }
}
```

---

## Implementation — `TickProximityCheck`

```cpp
void UHISMProxyBridgeComponent::TickProximityCheck()
{
    if (!SpatialGrid.IsBuilt()) { return; }

    const float ActRadiusSq   = Config->ActivationRadius * Config->ActivationRadius;
    const float DeactRadiusSq = FMath::Square(
        Config->ActivationRadius + Config->DeactivationRadiusBonus);

    // 1. Gather all player pawn positions.
    TArray<FVector> PlayerPositions;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
         It; ++It)
    {
        if (APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
            PlayerPositions.Add(Pawn->GetActorLocation());
    }
    if (PlayerPositions.IsEmpty()) { return; }

    // 2. Query spatial grid for all candidates near any player.
    TMap<int32, int32> InstancePlayerCount; // instance index → players in range
    for (const FVector& PlayerPos : PlayerPositions)
    {
        TArray<int32> Candidates;
        SpatialGrid.QueryRadius(PlayerPos, Config->ActivationRadius, Candidates);
        for (int32 Idx : Candidates)
        {
            const float DistSq = FVector::DistSquared(
                PlayerPos, SpatialGrid.GetInstancePosition(Idx));
            if (DistSq <= ActRadiusSq)
                InstancePlayerCount.FindOrAdd(Idx)++;
        }
    }

    // 3. Update slots for instances that are already managed.
    for (auto& [InstanceIdx, SlotIdx] : InstanceToSlotMap)
    {
        FHISMProxySlot& Slot = Slots[SlotIdx];
        Slot.PlayerRefCount = InstancePlayerCount.FindRef(InstanceIdx);

        if (Slot.PlayerRefCount > 0 &&
            Slot.State == EHISMProxySlotState::PendingRemoval)
        {
            // Player returned during hysteresis window — cancel timer.
            GetWorld()->GetTimerManager().ClearTimer(Slot.DeactivationTimer);
            Slot.State = EHISMProxySlotState::Active;
        }
        else if (Slot.PlayerRefCount == 0 &&
                 Slot.State == EHISMProxySlotState::Active)
        {
            BeginDeactivation(SlotIdx);
        }
    }

    // 4. Activate proxies for newly in-range instances.
    for (auto& [InstanceIdx, Count] : InstancePlayerCount)
    {
        if (InstanceToSlotMap.Contains(InstanceIdx)) { continue; }

        // Eligibility filter (optional, bound by game system).
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
        UE_LOG(LogGameCore, Warning,
            TEXT("UHISMProxyBridgeComponent [%s]: pool exhausted — instance %d skipped."),
            *GetName(), InstanceIndex);
        return;
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
    // PerInstanceCustomData[0] = hide flag.
    // HISM material reads this and clips the pixel when >= 0.5.
    TargetHISM->SetCustomDataValue(
        InstanceIndex, 0, bHidden ? 1.f : 0.f, /*bMarkRenderStateDirty=*/true);
}
```

---

## `EndPlay`

```cpp
void UHISMProxyBridgeComponent::EndPlay(EEndPlayReason::Type Reason)
{
    GetWorld()->GetTimerManager().ClearTimer(ProximityTickHandle);

    // Restore all HISM instances before teardown.
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        if (Slots[i].State != EHISMProxySlotState::Inactive)
        {
            DeactivateSlotImmediate(i);
        }
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

---

## Notes

- **`TargetHISM` is explicit.** No `FindComponentByClass` — avoids ambiguity when the host actor has multiple HISM components.
- **Single proxy class per bridge.** This is correct because each bridge manages a homogeneous HISM (one mesh type). For heterogeneous cases, override `ResolveProxyClass`.
- **Free list is now a flat `TArray<int32>`** (not `TMap<FGameplayTag, TArray<int32>>`). With one proxy class per bridge, tag-based routing is unnecessary, and the simpler flat free list is faster.
- **`FHISMProxySlot::TypeTag` removed.** The reverse-lookup during deactivation is gone — `FreeSlotIndices.Add(SlotIdx)` is O(1) with no tag lookup.
- **`EndPlay` restores HISM visibility.** Critical for PIE teardown and level unload — without this, hidden instances remain hidden permanently.
