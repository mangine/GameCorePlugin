// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HISMProxy/HISMInstanceSpatialGrid.h"
#include "HISMProxyBridgeComponent.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UHISMProxyConfig;
class AHISMProxyActor;

// ─────────────────────────────────────────────────────────────────────────────
// Delegate
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Returns true if InstanceIndex should receive a proxy when a player is nearby.
 * If unbound, all instances are treated as eligible.
 */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FHISMInstanceEligibilityDelegate,
    const UHierarchicalInstancedStaticMeshComponent* /*HISM*/,
    int32 /*InstanceIndex*/);

// ─────────────────────────────────────────────────────────────────────────────
// EHISMProxySlotState
// ─────────────────────────────────────────────────────────────────────────────

UENUM()
enum class EHISMProxySlotState : uint8
{
    Inactive,       // Pooled. Actor hidden, collision off.
    Active,         // Live. HISM instance hidden by CustomData.
    PendingRemoval  // Deactivation timer running. Reverts to Active if player re-enters.
};

// ─────────────────────────────────────────────────────────────────────────────
// FHISMProxySlot
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Plain struct — no GENERATED_BODY, not a UPROPERTY.
 * GC safety is provided by UHISMProxyBridgeComponent::AllPooledActors
 * (a UPROPERTY TArray). FHISMProxySlot::ProxyActor is a raw convenience pointer only.
 */
struct FHISMProxySlot
{
    int32               InstanceIndex  = INDEX_NONE;
    AHISMProxyActor*    ProxyActor     = nullptr;   // raw ptr — GC root is AllPooledActors
    EHISMProxySlotState State          = EHISMProxySlotState::Inactive;
    FTimerHandle        DeactivationTimer;
    int32               PlayerRefCount = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// UHISMProxyBridgeComponent
// ─────────────────────────────────────────────────────────────────────────────

/**
 * UHISMProxyBridgeComponent
 *
 * The central coordinator. Owns the proxy pool, drives the server-side
 * proximity tick, and manages the full lifecycle of each proxy slot.
 * One bridge manages exactly one HISM component.
 *
 * Auto-created by AHISMProxyHostActor per FHISMProxyInstanceType entry.
 * Tagged "HISMProxyManaged" for ownership tracking.
 */
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

    /**
     * Bind from a game subsystem to veto proxy activation for specific instances.
     * Called on the server only. If unbound, all instances are eligible.
     */
    FHISMInstanceEligibilityDelegate OnQueryInstanceEligibility;

    // ── Game System API ───────────────────────────────────────────────────────

    /**
     * Force-deactivates the active proxy for InstanceIndex (if any).
     * Use when an instance changes to a state that should suppress proxy activation
     * (e.g. resource depleted). The eligibility delegate will block re-activation.
     */
    UFUNCTION(BlueprintCallable, Category = "HISM Proxy")
    void NotifyInstanceStateChanged(int32 InstanceIndex);

    /** Returns the active or PendingRemoval proxy for InstanceIndex, or nullptr. */
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

    /**
     * Slot state. ProxyActor raw ptr is a convenience accessor;
     * GC ownership lives in AllPooledActors.
     */
    TArray<FHISMProxySlot> Slots;

    /** GC root for all pooled actors. Added in SpawnPoolActor, cleared in EndPlay. */
    UPROPERTY()
    TArray<TObjectPtr<AHISMProxyActor>> AllPooledActors;

    /** Instance index → slot index. Active and PendingRemoval entries only. */
    TMap<int32, int32> InstanceToSlotMap;

    /** Free list: indices into Slots[] that are currently Inactive. */
    TArray<int32> FreeSlotIndices;

    /**
     * Pre-allocated scratch buffers — member arrays to avoid per-tick heap allocation.
     * Reset() at tick start retains capacity; no allocator calls after first tick.
     * See AD-13 in Architecture.md.
     */
    TMap<int32, int32> TickInstancePlayerCount;
    TArray<FVector>    TickPlayerPositions;
    TArray<int32>      TickCandidates;
    TArray<int32>      TickSlotsToDeactivate;
    TArray<int32>      TickSlotsToRevive;

    FTimerHandle ProximityTickHandle;

    /** Set in BeginPlay before BuildPool. Pool actors spawn at host XY, -1km Z. */
    FVector PoolSpawnLocation = FVector::ZeroVector;
};
