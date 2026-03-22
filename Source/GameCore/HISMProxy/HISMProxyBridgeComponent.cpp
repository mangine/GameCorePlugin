// Copyright GameCore Plugin. All Rights Reserved.

#include "HISMProxy/HISMProxyBridgeComponent.h"
#include "HISMProxy/HISMProxyActor.h"
#include "HISMProxy/HISMProxyConfig.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCore, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

UHISMProxyBridgeComponent::UHISMProxyBridgeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// EndPlay
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Pool Management
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// TickProximityCheck
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Activation / Deactivation
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Game System API
// ─────────────────────────────────────────────────────────────────────────────

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
