#include "Spawning/SpawnManagerComponent.h"
#include "Spawning/ISpawnableEntity.h"
#include "Requirements/RequirementContext.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCoreSpawn, Log, All);

USpawnManagerComponent::USpawnManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(false);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void USpawnManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    // Server-only. No client path.
    if (!GetOwner() || !GetOwner()->HasAuthority())
        return;

    // Ensure SpawnPointConfig is valid.
    if (!SpawnPointConfig)
    {
        UE_LOG(LogGameCoreSpawn, Warning,
            TEXT("USpawnManagerComponent on [%s]: SpawnPointConfig is null. "
                 "Defaulting to USpawnPointConfig_RadiusRandom (Radius=500)."),
            *GetOwner()->GetName());
        SpawnPointConfig = NewObject<USpawnPointConfig_RadiusRandom>(this);
    }

    // Queue async loads for all soft class references.
    RequestAsyncClassLoads();

    // Start the flow timer (player count = 0 until first tick).
    ScheduleNextFlowTick(0);
}

void USpawnManagerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UWorld* World = GetWorld())
        World->GetTimerManager().ClearTimer(FlowTimerHandle);

    Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------
// Flow Timer
// ---------------------------------------------------------------------------

void USpawnManagerComponent::OnFlowTimerExpired()
{
    const int32 NearbyPlayers = bScaleByNearbyPlayers ? GetNearbyPlayerCount() : 0;

    // Reschedule BEFORE spawn work — work duration must not skew the interval.
    ScheduleNextFlowTick(NearbyPlayers);

    int32 Budget = GlobalFlowCount;

    for (FSpawnEntry& Entry : SpawnEntries)
    {
        if (Budget <= 0) break;

        Entry.GetAndPruneLiveCount(); // Safety net prune.
        if (Entry.GetVacancy() <= 0) continue;

        // Evaluate world-state requirements (no player context).
        if (Entry.SpawnRequirements)
        {
            FRequirementContext Ctx; // Empty context — spawn requirements are world-state only.
            if (!Entry.SpawnRequirements->Evaluate(Ctx).bPassed)
                continue;
        }

        const int32 ToSpawn = FMath::Min(
            Entry.GetVacancy(),
            Entry.GetEffectiveBudget(Budget));

        for (int32 i = 0; i < ToSpawn; ++i)
        {
            AActor* Spawned = TrySpawnForEntry(Entry);
            if (Spawned)
                Budget--; // Only successful spawns consume budget.
        }
    }
}

void USpawnManagerComponent::ScheduleNextFlowTick(int32 NearbyPlayers)
{
    const float Interval = ComputeNextInterval(NearbyPlayers);
    GetWorld()->GetTimerManager().SetTimer(
        FlowTimerHandle,
        this,
        &USpawnManagerComponent::OnFlowTimerExpired,
        Interval,
        /*bLoop=*/false);
}

float USpawnManagerComponent::ComputeNextInterval(int32 NearbyPlayers) const
{
    float Interval = BaseFlowInterval;

    if (bScaleByNearbyPlayers && NearbyPlayers > 0)
    {
        const float Alpha = FMath::Clamp(
            static_cast<float>(NearbyPlayers) /
            static_cast<float>(FMath::Max(PlayerCountForMinInterval, 1)),
            0.f, 1.f);
        Interval = FMath::Lerp(BaseFlowInterval, MinFlowInterval, Alpha);
    }

    Interval = FMath::Max(Interval, 10.f); // Hard floor.
    Interval += FMath::RandRange(0.f, 1.f); // Jitter.
    return Interval;
}

// ---------------------------------------------------------------------------
// Spawn Attempt
// ---------------------------------------------------------------------------

AActor* USpawnManagerComponent::TrySpawnForEntry(FSpawnEntry& Entry)
{
    if (Entry.EntityClass.IsNull()) return nullptr;
    UClass* LoadedClass = Entry.EntityClass.Get();
    if (!LoadedClass) return nullptr;

    FTransform SpawnTransform;
    if (!SpawnPointConfig->ResolveSpawnTransform(GetOwner(), SpawnTransform))
        return nullptr;

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
    Params.bDeferConstruction = true;

    AActor* Actor = GetWorld()->SpawnActor<AActor>(LoadedClass, SpawnTransform, Params);
    if (!Actor) return nullptr;

    // Bind destruction tracking before FinishSpawning.
    Actor->OnDestroyed.AddDynamic(this, &USpawnManagerComponent::OnSpawnedActorDestroyed);

    Actor->FinishSpawning(SpawnTransform);

    if (Actor->Implements<USpawnableEntity>())
        ISpawnableEntity::Execute_OnSpawnedByManager(Actor, GetOwner());

    Entry.LiveInstances.Add(Actor);
    return Actor;
}

void USpawnManagerComponent::OnSpawnedActorDestroyed(AActor* DestroyedActor)
{
    for (FSpawnEntry& Entry : SpawnEntries)
    {
        Entry.LiveInstances.RemoveAll(
            [DestroyedActor](const TWeakObjectPtr<AActor>& P)
            {
                return !P.IsValid() || P.Get() == DestroyedActor;
            });
    }
}

// ---------------------------------------------------------------------------
// Async Class Loading
// ---------------------------------------------------------------------------

void USpawnManagerComponent::RequestAsyncClassLoads()
{
    TArray<FSoftObjectPath> Paths;
    for (const FSpawnEntry& Entry : SpawnEntries)
    {
        if (!Entry.EntityClass.IsNull() && !Entry.EntityClass.IsValid())
            Paths.Add(Entry.EntityClass.ToSoftObjectPath());
    }
    if (Paths.IsEmpty()) return;

    UAssetManager::GetStreamableManager().RequestAsyncLoad(
        Paths, FStreamableDelegate::CreateUObject(
            this, &USpawnManagerComponent::OnClassesLoaded));
}

void USpawnManagerComponent::OnClassesLoaded()
{
    UE_LOG(LogGameCoreSpawn, Verbose,
        TEXT("USpawnManagerComponent on [%s]: async class loads complete."),
        GetOwner() ? *GetOwner()->GetName() : TEXT("unknown"));
}

// ---------------------------------------------------------------------------
// Player Count
// ---------------------------------------------------------------------------

int32 USpawnManagerComponent::GetNearbyPlayerCount() const
{
    if (OnCountNearbyPlayers)
        return OnCountNearbyPlayers(GetOwner()->GetActorLocation(), PlayerScanRadius);

    if (!bDelegateWarningLogged)
    {
        UE_LOG(LogGameCoreSpawn, Warning,
            TEXT("USpawnManagerComponent on [%s]: bScaleByNearbyPlayers is true but "
                 "OnCountNearbyPlayers is not bound. Interval scaling disabled."),
            *GetOwner()->GetName());
        bDelegateWarningLogged = true;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Loot Table Override
// ---------------------------------------------------------------------------

TSoftObjectPtr<ULootTable> USpawnManagerComponent::GetLootTableOverrideForClass(
    TSubclassOf<AActor> ActorClass) const
{
    if (!ActorClass) return nullptr;
    for (const FSpawnEntry& Entry : SpawnEntries)
    {
        if (Entry.EntityClass.Get() == ActorClass.Get())
            return Entry.LootTableOverride;
    }
    return nullptr;
}
