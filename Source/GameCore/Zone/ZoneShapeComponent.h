#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ZoneShapeComponent.generated.h"

// =============================================================================
// UZoneShapeComponent — Abstract Base
// =============================================================================

/**
 * Abstract base for zone shape components.
 * AZoneActor and UZoneSubsystem interact only through this interface.
 * Shape logic is fully encapsulated in concrete subclasses.
 */
UCLASS(Abstract)
class GAMECORE_API UZoneShapeComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    /** Returns true if WorldPoint is inside this shape. */
    UFUNCTION(BlueprintCallable)
    virtual bool ContainsPoint(const FVector& WorldPoint) const
        PURE_VIRTUAL(ContainsPoint, return false;);

    /** Returns a world-space AABB used for grid registration and broad-phase pre-filter. */
    virtual FBox GetWorldBounds() const
        PURE_VIRTUAL(GetWorldBounds, return FBox(EForceInit::ForceInit););

#if WITH_EDITOR
    virtual void DrawDebugShape(float Duration = 0.f) const {}
#endif
};

// =============================================================================
// UZoneBoxShapeComponent
// =============================================================================

/** Oriented box zone shape. Supports arbitrary rotation via cached inverse transform. */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class GAMECORE_API UZoneBoxShapeComponent : public UZoneShapeComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    FVector HalfExtent = FVector(500.f);

    virtual bool ContainsPoint(const FVector& WorldPoint) const override;
    virtual FBox GetWorldBounds() const override;

    virtual void OnRegister() override;
    virtual void OnUpdateTransform(EUpdateTransformFlags Flags, ETeleportType Teleport) override;

    /** Public so AZoneActor::InitializeZone can refresh after changing HalfExtent. */
    void RebuildCache();

private:
    FTransform CachedInverseTransform;
};

// =============================================================================
// UZoneConvexPolygonShapeComponent
// =============================================================================

/** Convex polygon (footprint + Z band) zone shape. */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class GAMECORE_API UZoneConvexPolygonShapeComponent : public UZoneShapeComponent
{
    GENERATED_BODY()

public:
    /** 2D footprint in local XY space. Min 3 points, wound CCW from above. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    TArray<FVector2D> LocalPolygonPoints;

    /** Z range in world space. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    float MinZ = -500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    float MaxZ = 500.f;

    virtual bool ContainsPoint(const FVector& WorldPoint) const override;
    virtual FBox GetWorldBounds() const override;

    virtual void OnRegister() override;

    /**
     * Must be called after modifying LocalPolygonPoints or actor transform.
     * Called automatically in OnRegister.
     * AZoneActor::InitializeZone calls this after setting polygon data at runtime.
     */
    void RebuildWorldPolygon();

private:
    TArray<FVector2D> WorldPolygon;
    FBox CachedBounds = FBox(EForceInit::ForceInit);

    static bool PointInConvexPolygon2D(const FVector2D& P, const TArray<FVector2D>& Poly);
};
