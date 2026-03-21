# FHISMInstanceSpatialGrid

**File:** `GameCore/Source/GameCore/Public/HISMProxy/HISMInstanceSpatialGrid.h / .cpp`  
**Module:** `GameCore`

Lightweight, server-only uniform 2D spatial grid built once over all HISM instance positions at `BeginPlay`. Enables O(cell-count) candidate lookup for a world position and radius — avoids a full O(N) linear scan of all instances every proximity tick.

Plain C++ struct — no `UObject` overhead, no GC involvement.

---

## Why a Uniform Grid

For static instances (R14) with a fixed, known query radius:

| | Uniform Grid | Octree |
|---|---|---|
| Build | O(N) | O(N log N) |
| Query | O(cells touched) ≈ O(1) | O(log N + results) |
| Memory | Flat arrays, cache-friendly | Pointer-chased tree |
| Complexity | Minimal | Moderate |

Octree would only be justified if instance density varied by several orders of magnitude across the map. For typical open-world prop placement it does not.

**Z axis:** The grid is 2D (XY). For world props with minimal vertical variance relative to activation radius, XY proximity is sufficient. The bridge performs a full 3D distance check on grid candidates. If your HISM spans large Z ranges, add a Z range check in the bridge's candidate validation loop.

---

## Class Definitions

```cpp
// A single grid cell.
struct FHISMSpatialCell
{
    // Instance indices whose XY position falls in this cell.
    // TArray acceptable here — cells are built once and never mutated.
    TArray<int32> InstanceIndices;
};

// Uniform 2D grid (XY plane) over HISM instance positions.
struct GAMECORE_API FHISMInstanceSpatialGrid
{
public:
    FHISMInstanceSpatialGrid() = default;

    // Build from a HISM component. Reads all instance world transforms.
    // CellSize should match UHISMProxyConfig::GridCellSize.
    // Call once at BeginPlay on the server.
    void Build(const UHierarchicalInstancedStaticMeshComponent* HISM, float CellSize);

    // Appends to OutCandidates all instance indices within QueryRadius of QueryCenter (XY).
    // OutCandidates is NOT cleared — caller is responsible.
    void QueryRadius(const FVector& QueryCenter, float QueryRadius,
                     TArray<int32>& OutCandidates) const;

    // Returns stored world position for InstanceIndex.
    // Avoids re-fetching from the HISM at query time.
    FORCEINLINE const FVector& GetInstancePosition(int32 InstanceIndex) const
    {
        return InstancePositions[InstanceIndex];
    }

    int32 GetInstanceCount() const { return InstancePositions.Num(); }
    bool  IsBuilt()          const { return bBuilt; }
    void  Reset();  // Clears all data; grid becomes unbuilt.

private:
    // World positions indexed by HISM instance index.
    // Stored here so the HISM component is not touched after build time.
    TArray<FVector> InstancePositions;

    TArray<FHISMSpatialCell> Cells;

    FVector GridOrigin  = FVector::ZeroVector;
    float   CellSize    = 0.f;
    int32   GridWidth   = 0;  // cells along X
    int32   GridHeight  = 0;  // cells along Y
    bool    bBuilt      = false;

    void  WorldToCell(const FVector& WorldPos, int32& OutCellX, int32& OutCellY) const;
    int32 GetCellIndex(int32 CellX, int32 CellY) const;
};
```

---

## Implementation — `Build`

```cpp
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
```

---

## Implementation — `QueryRadius`

```cpp
void FHISMInstanceSpatialGrid::QueryRadius(
    const FVector& QueryCenter, float QueryRadius, TArray<int32>& OutCandidates) const
{
    if (!bBuilt) { return; }

    const float RadiusSq = QueryRadius * QueryRadius;
    const int32 CellSpan = FMath::CeilToInt(QueryRadius / CellSize) + 1;

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
```

---

## Implementation — Helpers

```cpp
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
```

---

## Memory Estimate

For 1,000 static instances with `GridCellSize = 1500cm` over 500m × 500m:

- Grid: ~34 × 34 = ~1,156 cells
- `InstancePositions`: 1,000 × 12 bytes = ~12 KB
- `Cells` headers: 1,156 × ~24 bytes = ~28 KB
- `InstanceIndices` data: 1,000 × 4 bytes = ~4 KB
- **Total: ~44 KB** — negligible.

---

## Notes

- Built **once** at `BeginPlay`. No partial-rebuild API — static instances never move (R14). If the assumption breaks, call `Reset()` then `Build()`.
- Do not store a pointer to the HISM component inside the grid. Positions are copied at build time; the HISM is not touched again.
- Server-only. Clients have no knowledge of the grid.
- `QueryRadius` appends to `OutCandidates` — does not clear it. Caller (`TickProximityCheck`) calls `Reset()` on the scratch buffer before each query.
