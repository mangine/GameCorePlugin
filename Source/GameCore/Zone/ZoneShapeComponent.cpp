#include "ZoneShapeComponent.h"

// =============================================================================
// UZoneBoxShapeComponent
// =============================================================================

void UZoneBoxShapeComponent::RebuildCache()
{
    CachedInverseTransform = GetComponentTransform().Inverse();
}

void UZoneBoxShapeComponent::OnRegister()
{
    Super::OnRegister();
    RebuildCache();
}

void UZoneBoxShapeComponent::OnUpdateTransform(
    EUpdateTransformFlags Flags, ETeleportType Teleport)
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
    return FBox::BuildAABB(
        GetComponentLocation(),
        GetComponentTransform().TransformVector(HalfExtent).GetAbs());
}

// =============================================================================
// UZoneConvexPolygonShapeComponent
// =============================================================================

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
    if (WorldPoint.Z < MinZ || WorldPoint.Z > MaxZ) return false;
    return PointInConvexPolygon2D(FVector2D(WorldPoint.X, WorldPoint.Y), WorldPolygon);
}

FBox UZoneConvexPolygonShapeComponent::GetWorldBounds() const
{
    return CachedBounds;
}

// Cross-product sign consistency test for convex polygons.
// Uses KINDA_SMALL_NUMBER epsilon to guard against float precision on near-boundary points.
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
