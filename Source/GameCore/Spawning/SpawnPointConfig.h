#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SpawnPointConfig.generated.h"

UENUM(BlueprintType)
enum class ESpawnPointSelection : uint8
{
    Random     UMETA(DisplayName = "Random"),
    RoundRobin UMETA(DisplayName = "Round Robin"),
};

/**
 * Abstract spawn location strategy. Subclass and override ResolveSpawnTransform
 * to implement a new placement approach.
 *
 * Instances are owned by USpawnManagerComponent via EditInlineNew.
 * Must be stateless with respect to individual spawn attempts —
 * no per-spawn state may be stored (round-robin index is stable instance state).
 */
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API USpawnPointConfig : public UObject
{
    GENERATED_BODY()
public:
    /**
     * Attempt to resolve a world-space transform for one spawn attempt.
     * @return True if a valid transform was resolved; false to skip this attempt silently.
     */
    virtual bool ResolveSpawnTransform(
        AActor*     AnchorActor,
        FTransform& OutTransform) const
        PURE_VIRTUAL(USpawnPointConfig::ResolveSpawnTransform, return false;);

protected:
    /** Returns the first child USceneComponent on AnchorActor with ComponentTag. */
    static USceneComponent* FindChildComponentByTag(
        AActor* AnchorActor,
        FName   ComponentTag);

    /**
     * Collects all child USceneComponents on AnchorActor with ComponentTag.
     * Appends to OutComponents — callers must reset before calling if needed.
     */
    static void CollectChildComponentsByTag(
        AActor*                   AnchorActor,
        FName                     ComponentTag,
        TArray<USceneComponent*>& OutComponents);
};

// ---------------------------------------------------------------------------
// Radius Random Strategy
// ---------------------------------------------------------------------------

UCLASS(DisplayName = "Radius Random")
class GAMECORE_API USpawnPointConfig_RadiusRandom : public USpawnPointConfig
{
    GENERATED_BODY()
public:
    /** Tag of a child USceneComponent to use as the search centre. None = anchor root. */
    UPROPERTY(EditAnywhere, Category = "SpawnPoint")
    FName CenterComponentTag;

    /** Search radius in cm. */
    UPROPERTY(EditAnywhere, Category = "SpawnPoint", meta = (ClampMin = 50.f))
    float Radius = 500.f;

    /** Number of navmesh projection attempts before returning false. */
    UPROPERTY(EditAnywhere, Category = "SpawnPoint", meta = (ClampMin = 1, ClampMax = 10))
    int32 MaxProjectionAttempts = 3;

    virtual bool ResolveSpawnTransform(
        AActor*     AnchorActor,
        FTransform& OutTransform) const override;
};

// ---------------------------------------------------------------------------
// Point List Strategy
// ---------------------------------------------------------------------------

UCLASS(DisplayName = "Point List")
class GAMECORE_API USpawnPointConfig_PointList : public USpawnPointConfig
{
    GENERATED_BODY()
public:
    /**
     * Tags of child USceneComponents on the anchor actor.
     * All components matching any listed tag are pooled as candidates.
     */
    UPROPERTY(EditAnywhere, Category = "SpawnPoint")
    TArray<FName> PointComponentTags;

    UPROPERTY(EditAnywhere, Category = "SpawnPoint")
    ESpawnPointSelection Selection = ESpawnPointSelection::Random;

    virtual bool ResolveSpawnTransform(
        AActor*     AnchorActor,
        FTransform& OutTransform) const override;

private:
    mutable int32 RoundRobinIndex = 0;
};
