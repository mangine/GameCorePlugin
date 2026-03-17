# UHISMProxyBridgeComponent

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`UHISMProxyBridgeComponent` is the **central coordinator** of the HISM Proxy Actor System. It lives on the HISM host Actor, owns the proxy pool, drives the server-side proximity tick, and manages the full lifecycle of each proxy slot. Everything else in the system exists to support this component.

**Files:** `HISMProxy/HISMProxyBridgeComponent.h / .cpp`

---

## Delegates

```cpp
// Returns true if instance InstanceIndex should be eligible for a proxy
// when a player is nearby. Called per-instance during the proximity check.
// External game systems (harvesting, fishing) bind here to filter out
// instances that are on cooldown, depleted, or otherwise inert.
DECLARE_DELEGATE_RetVal_TwoParams(bool, FHISMInstanceEligibilityDelegate,
    const UHierarchicalInstancedStaticMeshComponent* /*HISM*/,
    int32 /*InstanceIndex*/);

// Returns the proxy type tag for a given instance.
// The returned tag is looked up in UHISMProxyConfig::ProxyClasses to select
// which pool to acquire from.
// Return an invalid tag to skip proxy activation for this instance.
DECLARE_DELEGATE_RetVal_TwoParams(FGameplayTag, FHISMInstanceTypeDelegate,
    const UHierarchicalInstancedStaticMeshComponent* /*HISM*/,
    int32 /*InstanceIndex*/);
```

---

## Slot State

```cpp
UENUM()
enum class EHISMProxySlotState : uint8
{
    // Proxy is in the free pool. Actor is hidden, collision disabled.
    Inactive,

    // Proxy is live. Actor is visible, positioned at the HISM instance.
    // HISM instance is hidden via PerInstanceCustomData.
    Active,

    // Player(s) have left range. Deactivation timer is running.
    // If a new player enters range, the timer is cancelled and state returns to Active.
    PendingRemoval
};

struct FHISMProxySlot
{
    // The HISM instance index this slot currently represents. INDEX_NONE if Inactive.
    int32 InstanceIndex = INDEX_NONE;

    // The proxy actor assigned to this slot. Always valid (pre-allocated).
    // Never null after BeginPlay.
    TObjectPtr<AHISMProxyActor> ProxyActor = nullptr;

    // Current lifecycle state.
    EHISMProxySlotState State = EHISMProxySlotState::Inactive;

    // Timer handle for the deactivation delay. Only active in PendingRemoval state.
    FTimerHandle DeactivationTimer;

    // Number of players currently within the effective activation range.
    // Proxy deactivation is only initiated when this reaches 0.
    int32 PlayerRefCount = 0;
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

    // Required. Set in the editor on the host Actor's component.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HISM Proxy")
    TObjectPtr<UHISMProxyConfig> Config;

    // ── External Delegate Hooks ───────────────────────────────────────────────
    // Bind these from your game system before or at BeginPlay.

    // "Should this instance get a proxy when a player is nearby?"
    // If unbound, all instances are treated as eligible.
    FHISMInstanceEligibilityDelegate OnQueryInstanceEligibility;

    // "What proxy type tag should this instance use?"
    // If unbound, the first entry in Config->ProxyClasses is used for all instances.
    FHISMInstanceTypeDelegate OnQueryInstanceType;

    // ── Game System API ───────────────────────────────────────────────────────

    // Call when instance state changes externally (e.g. resource depleted).
    // If the instance has an active proxy, forces immediate deactivation.
    // If the instance has a pending-removal proxy, cancels the timer and deactivates.
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    void NotifyInstanceStateChanged(int32 InstanceIndex);

    // Returns the active proxy actor for the given instance, or nullptr if inactive.
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    AHISMProxyActor* GetActiveProxy(int32 InstanceIndex) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;

private:
    // ── Pool Management ───────────────────────────────────────────────────────

    // Pre-allocates all proxy actors into PoolByType free lists.
    // Called once at BeginPlay. All actors are spawned hidden, collision disabled.
    void BuildPool();

    // Returns the Inactive proxy from the free list for the given type tag.
    // Returns nullptr if the pool for that type is exhausted.
    AHISMProxyActor* AcquireFromPool(const FGameplayTag& TypeTag);

    // Returns a proxy actor to its free list. Hides it and clears its slot.
    void ReturnToPool(AHISMProxyActor* Proxy, const FGameplayTag& TypeTag);

    // ── Proximity Tick ───────────────────────────────────────────────────────

    // Registered with the world timer. Runs at Config->ProximityTickInterval.
    void TickProximityCheck();

    // ── Slot Lifecycle ───────────────────────────────────────────────────────

    // Acquires a slot + proxy and activates them for the given instance.
    void ActivateProxyForInstance(int32 InstanceIndex, const FGameplayTag& TypeTag,
                                   const FTransform& InstanceWorldTransform);

    // Starts the deactivation timer for a slot. Transitions to PendingRemoval.
    void BeginDeactivation(int32 SlotIndex);

    // Timer callback — deactivates the proxy and returns it to the pool.
    void OnDeactivationTimerFired(int32 SlotIndex);

    // Immediately deactivates a slot regardless of timer state.
    void DeactivateSlotImmediate(int32 SlotIndex);

    // ── HISM Instance Visibility ──────────────────────────────────────────────

    // Writes PerInstanceCustomData[0] = 1.0 (hide) or 0.0 (show) for the instance.
    void SetHISMInstanceHidden(int32 InstanceIndex, bool bHidden);

    // ── Data ──────────────────────────────────────────────────────────────────

    // Weak reference to the sibling HISM component on the same Actor.
    UPROPERTY()
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> CachedHISM;

    // Spatial grid — built at BeginPlay from CachedHISM.
    FHISMInstanceSpatialGrid SpatialGrid;

    // All slots (active + inactive). Size == Config->PoolSize.
    TArray<FHISMProxySlot> Slots;

    // HISM instance index → slot array index. Only contains Active/PendingRemoval entries.
    TMap<int32, int32> InstanceToSlotMap;

    // Free list per type tag. Maps tag → indices into Slots[] that are Inactive.
    TMap<FGameplayTag, TArray<int32>> FreeSlotsByType;

    // Proximity tick timer handle.
    FTimerHandle ProximityTickHandle;
};
```

---

## Implementation — `BeginPlay`

```cpp
void UHISMProxyBridgeComponent::BeginPlay()
{
    Super::BeginPlay();

    // Server only — clients have no pool, no proximity tick.
    if (!GetOwner()->HasAuthority()) { return; }

    // Validate config.
    if (!Config)
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UHISMProxyBridgeComponent on %s has no Config asset — disabled."),
            *GetOwner()->GetName());
        return;
    }

    // Find the sibling HISM component.
    CachedHISM = GetOwner()->FindComponentByClass<UHierarchicalInstancedStaticMeshComponent>();
    if (!CachedHISM)
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UHISMProxyBridgeComponent: no UHierarchicalInstancedStaticMeshComponent "
                 "found on %s — disabled."), *GetOwner()->GetName());
        return;
    }

    // Build spatial grid.
    SpatialGrid.Build(CachedHISM, Config->GridCellSize);

    // Pre-allocate pool.
    BuildPool();

    // Start proximity tick.
    GetWorld()->GetTimerManager().SetTimer(
        ProximityTickHandle,
        this,
        &UHISMProxyBridgeComponent::TickProximityCheck,
        Config->ProximityTickInterval,
        /*bLoop=*/true);
}
```

---

## Implementation — `BuildPool`

```cpp
void UHISMProxyBridgeComponent::BuildPool()
{
    int32 SlotIndex = 0;
    Slots.Reserve(Config->PoolSize);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    for (const FHISMProxyClassEntry& Entry : Config->ProxyClasses)
    {
        if (!Entry.ProxyClass) { continue; }

        TArray<int32>& FreeList = FreeSlotsByType.FindOrAdd(Entry.ProxyTypeTag);

        for (int32 i = 0; i < Entry.PoolAllocation; ++i)
        {
            // Spawn hidden at world origin. Actual transform set on activation.
            AHISMProxyActor* Actor = GetWorld()->SpawnActor<AHISMProxyActor>(
                Entry.ProxyClass, FTransform::Identity, SpawnParams);

            check(Actor); // Should not fail — AlwaysSpawn at origin
            Actor->SetActorHiddenInGame(true);
            Actor->SetActorEnableCollision(false);

            FHISMProxySlot& Slot = Slots.AddDefaulted_GetRef();
            Slot.ProxyActor = Actor;
            Slot.State      = EHISMProxySlotState::Inactive;

            FreeList.Add(SlotIndex);
            ++SlotIndex;
        }
    }
}
```

---

## Implementation — `TickProximityCheck`

```cpp
void UHISMProxyBridgeComponent::TickProximityCheck()
{
    if (!SpatialGrid.IsBuilt()) { return; }

    UWorld* World = GetWorld();
    const float ActRadius    = Config->ActivationRadius;
    const float DeactRadius  = ActRadius + Config->DeactivationRadiusBonus;
    const float DeactRadiusSq = DeactRadius * DeactRadius;

    // --- Gather all player pawn positions ---
    TArray<FVector> PlayerPositions;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
         It; ++It)
    {
        if (APawn* Pawn = It->Get()->GetPawn())
        {
            PlayerPositions.Add(Pawn->GetActorLocation());
        }
    }

    if (PlayerPositions.IsEmpty()) { return; }

    // --- Build per-player candidate sets from spatial grid ---
    // Key: instance index, Value: number of players in range
    TMap<int32, int32> InstancePlayerCount;

    for (const FVector& PlayerPos : PlayerPositions)
    {
        TArray<int32> Candidates;
        SpatialGrid.QueryRadius(PlayerPos, ActRadius, Candidates);

        for (int32 InstanceIdx : Candidates)
        {
            // Full 3D distance check (grid query is XY only).
            const FVector InstancePos = SpatialGrid.GetInstancePosition(InstanceIdx);
            const float DistSq = FVector::DistSquared(PlayerPos, InstancePos);
            if (DistSq <= ActRadius * ActRadius)
            {
                InstancePlayerCount.FindOrAdd(InstanceIdx)++;
            }
        }
    }

    // --- Update active/pending slots for players leaving range ---
    for (auto& [InstanceIdx, SlotIdx] : InstanceToSlotMap)
    {
        FHISMProxySlot& Slot = Slots[SlotIdx];
        const int32 InRangeCount = InstancePlayerCount.FindRef(InstanceIdx);
        Slot.PlayerRefCount = InRangeCount;

        if (InRangeCount > 0 && Slot.State == EHISMProxySlotState::PendingRemoval)
        {
            // Player came back — cancel pending deactivation.
            World->GetTimerManager().ClearTimer(Slot.DeactivationTimer);
            Slot.State = EHISMProxySlotState::Active;
        }
        else if (InRangeCount == 0 && Slot.State == EHISMProxySlotState::Active)
        {
            BeginDeactivation(SlotIdx);
        }
    }

    // --- Activate proxies for new in-range instances ---
    for (auto& [InstanceIdx, InRangeCount] : InstancePlayerCount)
    {
        if (InstanceToSlotMap.Contains(InstanceIdx)) { continue; } // already managed

        // Eligibility check via external delegate.
        if (OnQueryInstanceEligibility.IsBound() &&
            !OnQueryInstanceEligibility.Execute(CachedHISM, InstanceIdx))
        {
            continue;
        }

        // Type resolution via external delegate.
        FGameplayTag TypeTag;
        if (OnQueryInstanceType.IsBound())
        {
            TypeTag = OnQueryInstanceType.Execute(CachedHISM, InstanceIdx);
        }
        else if (!Config->ProxyClasses.IsEmpty())
        {
            TypeTag = Config->ProxyClasses[0].ProxyTypeTag;
        }

        if (!TypeTag.IsValid()) { continue; }

        FTransform InstanceTransform;
        CachedHISM->GetInstanceTransform(InstanceIdx, InstanceTransform, /*bWorldSpace=*/true);

        ActivateProxyForInstance(InstanceIdx, TypeTag, InstanceTransform);
    }
}
```

---

## Implementation — `ActivateProxyForInstance`

```cpp
void UHISMProxyBridgeComponent::ActivateProxyForInstance(
    int32 InstanceIndex, const FGameplayTag& TypeTag, const FTransform& InstanceWorldTransform)
{
    AHISMProxyActor* Proxy = AcquireFromPool(TypeTag);
    if (!Proxy)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("UHISMProxyBridgeComponent: pool exhausted for tag %s — instance %d skipped."),
            *TypeTag.ToString(), InstanceIndex);
        return;
    }

    // Find the slot that owns this proxy.
    int32 SlotIdx = INDEX_NONE;
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        if (Slots[i].ProxyActor == Proxy) { SlotIdx = i; break; }
    }
    check(SlotIdx != INDEX_NONE);

    FHISMProxySlot& Slot = Slots[SlotIdx];
    Slot.InstanceIndex   = InstanceIndex;
    Slot.State           = EHISMProxySlotState::Active;
    Slot.PlayerRefCount  = 1; // at least one player caused this activation

    InstanceToSlotMap.Add(InstanceIndex, SlotIdx);

    // Position the proxy.
    Proxy->SetActorTransform(InstanceWorldTransform);
    Proxy->SetActorHiddenInGame(false);
    Proxy->SetActorEnableCollision(true);

    // Hide the HISM instance so mesh and proxy don't overlap.
    SetHISMInstanceHidden(InstanceIndex, /*bHidden=*/true);

    // Notify the proxy actor.
    Proxy->OnProxyActivated(InstanceIndex, InstanceWorldTransform);
}
```

---

## Implementation — Deactivation

```cpp
void UHISMProxyBridgeComponent::BeginDeactivation(int32 SlotIdx)
{
    FHISMProxySlot& Slot = Slots[SlotIdx];
    Slot.State = EHISMProxySlotState::PendingRemoval;

    FTimerDelegate Delegate;
    Delegate.BindUObject(this, &UHISMProxyBridgeComponent::OnDeactivationTimerFired, SlotIdx);

    GetWorld()->GetTimerManager().SetTimer(
        Slot.DeactivationTimer,
        Delegate,
        Config->DeactivationDelay,
        /*bLoop=*/false);
}

void UHISMProxyBridgeComponent::OnDeactivationTimerFired(int32 SlotIdx)
{
    DeactivateSlotImmediate(SlotIdx);
}

void UHISMProxyBridgeComponent::DeactivateSlotImmediate(int32 SlotIdx)
{
    FHISMProxySlot& Slot = Slots[SlotIdx];
    if (Slot.State == EHISMProxySlotState::Inactive) { return; }

    // Cancel any pending timer.
    GetWorld()->GetTimerManager().ClearTimer(Slot.DeactivationTimer);

    const int32 InstanceIndex = Slot.InstanceIndex;

    // Notify proxy before hiding.
    Slot.ProxyActor->OnProxyDeactivated();

    // Hide proxy and remove collision.
    Slot.ProxyActor->SetActorHiddenInGame(true);
    Slot.ProxyActor->SetActorEnableCollision(false);

    // Restore HISM instance visibility.
    SetHISMInstanceHidden(InstanceIndex, /*bHidden=*/false);

    // Determine the type tag to return to the correct free list.
    FGameplayTag TypeTag;
    if (OnQueryInstanceType.IsBound())
        TypeTag = OnQueryInstanceType.Execute(CachedHISM, InstanceIndex);
    else if (!Config->ProxyClasses.IsEmpty())
        TypeTag = Config->ProxyClasses[0].ProxyTypeTag;

    // Clean up slot.
    InstanceToSlotMap.Remove(InstanceIndex);
    Slot.InstanceIndex  = INDEX_NONE;
    Slot.State          = EHISMProxySlotState::Inactive;
    Slot.PlayerRefCount = 0;

    // Return proxy to free list.
    if (TypeTag.IsValid())
    {
        if (TArray<int32>* FreeList = FreeSlotsByType.Find(TypeTag))
        {
            FreeList->Add(SlotIdx);
        }
    }
}
```

---

## Implementation — Pool Helpers

```cpp
AHISMProxyActor* UHISMProxyBridgeComponent::AcquireFromPool(const FGameplayTag& TypeTag)
{
    TArray<int32>* FreeList = FreeSlotsByType.Find(TypeTag);
    if (!FreeList || FreeList->IsEmpty()) { return nullptr; }

    const int32 SlotIdx = FreeList->Pop(/*bAllowShrinking=*/false);
    return Slots[SlotIdx].ProxyActor;
}

void UHISMProxyBridgeComponent::SetHISMInstanceHidden(int32 InstanceIndex, bool bHidden)
{
    // PerInstanceCustomData[0] is reserved by this system as the hide flag.
    // The HISM material must read this slot and clip the pixel when value >= 0.5.
    CachedHISM->SetCustomDataValue(
        InstanceIndex,
        /*CustomDataIndex=*/0,
        bHidden ? 1.f : 0.f,
        /*bMarkRenderStateDirty=*/true);
}
```

---

## `NotifyInstanceStateChanged`

```cpp
void UHISMProxyBridgeComponent::NotifyInstanceStateChanged(int32 InstanceIndex)
{
    if (!GetOwner()->HasAuthority()) { return; }

    if (const int32* SlotIdx = InstanceToSlotMap.Find(InstanceIndex))
    {
        DeactivateSlotImmediate(*SlotIdx);
    }
}
```

Call this when instance state changes externally — e.g. a resource node was harvested and should no longer have an interactive proxy.

---

## Notes

- **Server-only.** `BeginPlay` returns immediately on non-authority machines. Clients never touch the pool.
- **One HISM component per bridge.** `FindComponentByClass` returns the first match. If the host Actor has multiple HISM components, only one will be managed. Future extension can support a manual component reference via an `EditAnywhere` `TObjectPtr<UHierarchicalInstancedStaticMeshComponent>`.
- **`PerInstanceCustomData` slot 0 is consumed** by this system for the hide flag. Game-specific custom data must use slots 1 and above. Ensure `NumCustomDataFloats >= 1` is set on the HISM component in the editor.
- **Pool slot → type tag reverse lookup** during `DeactivateSlotImmediate` re-queries the delegate. This is acceptable at 0.5s intervals but can be optimised by caching the type tag in `FHISMProxySlot` if profiling reveals it as hot.
- **`EndPlay`** should clear the proximity timer and call `DeactivateSlotImmediate` on all active slots to restore HISM instance visibility cleanly before the host Actor is destroyed.
