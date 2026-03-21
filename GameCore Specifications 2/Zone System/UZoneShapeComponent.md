# UZoneShapeComponent (and Concrete Variants)

**Files:**
- `GameCore/Source/GameCore/Zone/ZoneShapeComponent.h` / `.cpp`

Abstract base + two concrete shape components. Shape logic is fully encapsulated — `AZoneActor` and `UZoneSubsystem` interact only through the base interface.

---

## `UZoneShapeComponent` (Abstract Base)

```cpp
UCLASS(Abstract)
class GAMECORE_API UZoneShapeComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    // Returns true if WorldPoint is inside this shape.
    UFUNCTION(BlueprintCallable)
    virtual bool ContainsPoint(const FVector& WorldPoint) const
        PURE_VIRTUAL(ContainsPoint, return false;);

    // Returns a world-space AABB used for grid registration and broad-phase pre-filter.
    virtual FBox GetWorldBounds() const
        PURE_VIRTUAL(GetWorldBounds, return FBox(EForceInit::ForceInit););

#if WITH_EDITOR
    virtual void DrawDebugShape(float Duration = 0.f) const {}
#endif
};
```

---

## `UZoneBoxShapeComponent`

```cpp
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

private:
    // Cached inverse transform, updated on OnRegister and OnUpdateTransform.
    FTransform CachedInverseTransform;

    void RebuildCache();
};
```

### Implementation

```cpp
void UZoneBoxShapeComponent::RebuildCache()
{
    CachedInverseTransform = GetComponentTransform().Inverse();
}

void UZoneBoxShapeComponent::OnRegister()
{
    Super::OnRegister();
    RebuildCache();
}

void UZoneBoxShapeComponent::OnUpdateTransform(EUpdateTransformFlags Flags, ETeleportType Teleport)
{
    Super::OnUpdateTransform(Flags, Teleport);
    RebuildCache();
}

bool UZoneBoxShapeComponent::ContainsPoint(const FVector& WorldPoint) const
{
    const FVector Local = CachedInverseTransform.TransformPosition(WorldPoint);
    return FMath::Abs(Local.X) <= HalfExtent.X
        && FMath::Abs(Local.Y) <= HalfExtent.Y
        && FMath::Abs(Local.Z) <= HalfExtent.Z;
}

FBox UZoneBoxShapeComponent::GetWorldBounds() const
{
    return FBox::BuildAABB(GetComponentLocation(), GetComponentTransform().TransformVector(HalfExtent).GetAbs());
}
```

> **Note:** Zones are static after placement. `OnUpdateTransform` fires during editor manipulation and during `InitializeZone` at runtime — keeping the cache valid in both contexts.

---

## `UZoneConvexPolygonShapeComponent`

```cpp
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class GAMECORE_API UZoneConvexPolygonShapeComponent : public UZoneShapeComponent
{
    GENERATED_BODY()

public:
    // 2D footprint in local XY space. Min 3 points, wound CCW from above.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    TArray<FVector2D> LocalPolygonPoints;

    // Z range in world space.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    float MinZ = -500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    float MaxZ = 500.f;

    virtual bool ContainsPoint(const FVector& WorldPoint) const override;
    virtual FBox GetWorldBounds() const override;

    virtual void OnRegister() override;

    // Must be called after modifying LocalPolygonPoints or actor transform.
    // Called automatically in OnRegister.
    void RebuildWorldPolygon();

private:
    // Precomputed world-space 2D polygon (XY only).
    TArray<FVector2D> WorldPolygon;
    FBox CachedBounds;

    // Returns true if P is inside the convex polygon.
    // Uses cross-product sign consistency with epsilon guard.
    static bool PointInConvexPolygon2D(const FVector2D& P, const TArray<FVector2D>& Poly);
};
```

### Implementation

```cpp
void UZoneConvexPolygonShapeComponent::OnRegister()
{
    Super::OnRegister();
    RebuildWorldPolygon();
}

void UZoneConvexPolygonShapeComponent::RebuildWorldPolygon()
{
    WorldPolygon.Reset(LocalPolygonPoints.Num());
    const FTransform& T = GetComponentTransform();

    FBox2D Bounds2D(EForceInit::ForceInit);
    for (const FVector2D& Local : LocalPolygonPoints)
    {
        // Transform XY local point through the component transform (ignoring Z)
        const FVector World = T.TransformPosition(FVector(Local.X, Local.Y, 0.f));
        const FVector2D World2D(World.X, World.Y);
        WorldPolygon.Add(World2D);
        Bounds2D += World2D;
    }

    CachedBounds = FBox(
        FVector(Bounds2D.Min.X, Bounds2D.Min.Y, MinZ),
        FVector(Bounds2D.Max.X, Bounds2D.Max.Y, MaxZ));
}

bool UZoneConvexPolygonShapeComponent::ContainsPoint(const FVector& WorldPoint) const
{
    // Height band check first (cheap early-out)
    if (WorldPoint.Z < MinZ || WorldPoint.Z > MaxZ) return false;
    return PointInConvexPolygon2D(FVector2D(WorldPoint.X, WorldPoint.Y), WorldPolygon);
}

FBox UZoneConvexPolygonShapeComponent::GetWorldBounds() const
{
    return CachedBounds;
}

// Cross-product sign consistency test for convex polygons.
// Uses KINDA_SMALL_NUMBER epsilon to guard against float precision issues on near-boundary points.
bool UZoneConvexPolygonShapeComponent::PointInConvexPolygon2D(
    const FVector2D& P, const TArray<FVector2D>& Poly)
{
    const int32 N = Poly.Num();
    if (N < 3) return false;

    float LastCross = 0.f;
    for (int32 i = 0; i < N; ++i)
    {
        const FVector2D& A = Poly[i];
        const FVector2D& B = Poly[(i + 1) % N];
        const FVector2D Edge = B - A;
        const FVector2D ToP  = P - A;
        const float Cross = Edge.X * ToP.Y - Edge.Y * ToP.X;

        if (FMath::Abs(Cross) < KINDA_SMALL_NUMBER) continue; // collinear — skip

        if (LastCross != 0.f && FMath::Sign(Cross) != FMath::Sign(LastCross))
            return false;

        LastCross = Cross;
    }
    return true;
}
```

> **Important:** `RebuildWorldPolygon()` must be called explicitly after `LocalPolygonPoints` or the component transform changes outside of `OnRegister` (e.g. after `InitializeZone` sets polygon data at runtime). `AZoneActor::InitializeZone` is responsible for triggering this.
