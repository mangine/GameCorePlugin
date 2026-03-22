#include "Spawning/SpawnPointConfig.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

// ---------------------------------------------------------------------------
// Base helpers
// ---------------------------------------------------------------------------

USceneComponent* USpawnPointConfig::FindChildComponentByTag(
    AActor* AnchorActor, FName ComponentTag)
{
    if (!AnchorActor || ComponentTag.IsNone()) return nullptr;
    TArray<USceneComponent*> All;
    AnchorActor->GetComponents<USceneComponent>(All);
    for (USceneComponent* Comp : All)
    {
        if (Comp && Comp->ComponentTags.Contains(ComponentTag))
            return Comp;
    }
    return nullptr;
}

void USpawnPointConfig::CollectChildComponentsByTag(
    AActor* AnchorActor, FName ComponentTag,
    TArray<USceneComponent*>& OutComponents)
{
    if (!AnchorActor || ComponentTag.IsNone()) return;
    TArray<USceneComponent*> All;
    AnchorActor->GetComponents<USceneComponent>(All);
    for (USceneComponent* Comp : All)
    {
        if (Comp && Comp->ComponentTags.Contains(ComponentTag))
            OutComponents.Add(Comp);
    }
}

// ---------------------------------------------------------------------------
// RadiusRandom
// ---------------------------------------------------------------------------

bool USpawnPointConfig_RadiusRandom::ResolveSpawnTransform(
    AActor* AnchorActor, FTransform& OutTransform) const
{
    if (!AnchorActor) return false;

    FVector Center = AnchorActor->GetActorLocation();
    if (!CenterComponentTag.IsNone())
    {
        if (USceneComponent* Comp = FindChildComponentByTag(AnchorActor, CenterComponentTag))
            Center = Comp->GetComponentLocation();
    }

    UNavigationSystemV1* NavSys =
        FNavigationSystem::GetCurrent<UNavigationSystemV1>(AnchorActor->GetWorld());
    if (!NavSys) return false;

    for (int32 i = 0; i < MaxProjectionAttempts; ++i)
    {
        const FVector2D RandDir = FMath::RandPointInCircle(Radius);
        const FVector Candidate = Center + FVector(RandDir.X, RandDir.Y, 0.f);

        FNavLocation NavLocation;
        if (NavSys->ProjectPointToNavigation(
                Candidate, NavLocation,
                FVector(Radius, Radius, 500.f)))
        {
            OutTransform = FTransform(NavLocation.Location);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// PointList
// ---------------------------------------------------------------------------

bool USpawnPointConfig_PointList::ResolveSpawnTransform(
    AActor* AnchorActor, FTransform& OutTransform) const
{
    if (!AnchorActor || PointComponentTags.IsEmpty()) return false;

    TArray<USceneComponent*> Candidates;
    for (const FName& Tag : PointComponentTags)
        CollectChildComponentsByTag(AnchorActor, Tag, Candidates);

    if (Candidates.IsEmpty()) return false;

    USceneComponent* Chosen = nullptr;
    if (Selection == ESpawnPointSelection::Random)
    {
        Chosen = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
    }
    else // RoundRobin
    {
        RoundRobinIndex = RoundRobinIndex % Candidates.Num();
        Chosen = Candidates[RoundRobinIndex];
        ++RoundRobinIndex;
    }

    OutTransform = Chosen->GetComponentTransform();
    return true;
}
