// Copyright GameCore Plugin. All Rights Reserved.

#include "HISMProxy/HISMInstanceSpatialGrid.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCore, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
// Build
// ─────────────────────────────────────────────────────────────────────────────

void FHISMInstanceSpatialGrid::Build(
    const UHierarchicalInstancedStaticMeshComponent* HISM, float InCellSize)
{
    check(HISM && InCellSize > 0.f);
    Reset();
    CellSize = InCellSize;

    const int32 NumInstances = HISM->GetInstanceCount();
    if (NumInstances == 0) { return; }

    // Pass 1: collect world positions and compute AABB
    InstancePositions.Reserve(NumInstances);
    FBox Bounds(EForceInit::ForceInit);

    for (int32 i = 0; i < NumInstances; ++i)
    {
        FTransform T;
        HISM->GetInstanceTransform(i, T, /*bWorldSpace=*/true);
        const FVector Pos = T.GetTranslation();
        InstancePositions.Add(Pos);
        Bounds += Pos;
    }

    // Compute grid dimensions — expand by one cell to avoid edge clamping
    const FVector BoundsMin = Bounds.Min - CellSize;
    const FVector BoundsMax = Bounds.Max + CellSize;
    GridOrigin = BoundsMin;

    GridWidth  = FMath::CeilToInt((BoundsMax.X - BoundsMin.X) / CellSize);
    GridHeight = FMath::CeilToInt((BoundsMax.Y - BoundsMin.Y) / CellSize);

    Cells.SetNum(GridWidth * GridHeight);

    // Pass 2: insert each instance into its cell
    for (int32 i = 0; i < NumInstances; ++i)
    {
        int32 CX, CY;
        WorldToCell(InstancePositions[i], CX, CY);
        const int32 CellIdx = GetCellIndex(CX, CY);
        if (CellIdx != INDEX_NONE)
            Cells[CellIdx].InstanceIndices.Add(i);
    }

    bBuilt = true;

    UE_LOG(LogGameCore, Verbose,
        TEXT("FHISMInstanceSpatialGrid: built %d instances into %dx%d cells (%.0fcm)."),
        NumInstances, GridWidth, GridHeight, CellSize);
}

// ─────────────────────────────────────────────────────────────────────────────
// QueryRadius
// ─────────────────────────────────────────────────────────────────────────────

void FHISMInstanceSpatialGrid::QueryRadius(
    const FVector& QueryCenter, float QueryRadius, TArray<int32>& OutCandidates) const
{
    if (!bBuilt) { return; }

    const float RadiusSq  = QueryRadius * QueryRadius;
    const int32 CellSpan  = FMath::CeilToInt(QueryRadius / CellSize) + 1;

    int32 CenterCX, CenterCY;
    WorldToCell(QueryCenter, CenterCX, CenterCY);

    for (int32 DY = -CellSpan; DY <= CellSpan; ++DY)
    {
        for (int32 DX = -CellSpan; DX <= CellSpan; ++DX)
        {
            const int32 CellIdx = GetCellIndex(CenterCX + DX, CenterCY + DY);
            if (CellIdx == INDEX_NONE) { continue; }

            for (int32 InstanceIdx : Cells[CellIdx].InstanceIndices)
            {
                const FVector& InstancePos = InstancePositions[InstanceIdx];
                const float Dx = InstancePos.X - QueryCenter.X;
                const float Dy = InstancePos.Y - QueryCenter.Y;
                if ((Dx * Dx + Dy * Dy) <= RadiusSq)
                    OutCandidates.Add(InstanceIdx);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Reset
// ─────────────────────────────────────────────────────────────────────────────

void FHISMInstanceSpatialGrid::Reset()
{
    InstancePositions.Reset();
    Cells.Reset();
    GridOrigin = FVector::ZeroVector;
    CellSize   = 0.f;
    GridWidth  = 0;
    GridHeight = 0;
    bBuilt     = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void FHISMInstanceSpatialGrid::WorldToCell(
    const FVector& WorldPos, int32& OutCellX, int32& OutCellY) const
{
    OutCellX = FMath::FloorToInt((WorldPos.X - GridOrigin.X) / CellSize);
    OutCellY = FMath::FloorToInt((WorldPos.Y - GridOrigin.Y) / CellSize);
}

int32 FHISMInstanceSpatialGrid::GetCellIndex(int32 CellX, int32 CellY) const
{
    if (CellX < 0 || CellX >= GridWidth || CellY < 0 || CellY >= GridHeight)
        return INDEX_NONE;
    return CellY * GridWidth + CellX;
}
