# FHISMInstanceSpatialGrid

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`FHISMInstanceSpatialGrid` is a lightweight, server-only **uniform spatial grid** built once over all HISM instance positions at `BeginPlay`. It enables O(cell-count) candidate lookup for a given world position and radius, avoiding a full O(N) linear scan of all instances on every proximity tick.

This is a plain C++ struct — no `UObject` overhead, no GC involvement.

**Files:** `HISMProxy/HISMInstanceSpatialGrid.h / .cpp`

---

## Why a Uniform Grid (not an Octree)

| Criterion | Uniform Grid | Octree |
|---|---|---|
| Build time | O(N) | O(N log N) |
| Query time | O(cells touched) ≈ O(1) for fixed radius | O(log N + results) |
| Memory | Proportional to world bounds / cell size | Proportional to N |
| Implementation complexity | Minimal | Moderate |
| Cache friendliness | High (flat arrays) | Low (pointer-chased tree) |
| Suitable for static data | **Yes** | Yes |

For static HISM instances where the query radius is fixed and known at build time, the uniform grid wins on every axis that matters. An octree would be justified only if instance density varied by many orders of magnitude across the world (e.g. 10,000 instances in one 100m area and 1 per km² elsewhere).

---

## Class Definition

```cpp
// A single cell in the grid. Stores indices into the master instance array.
struct FHISMSpatialCell
{
    // Indices of HISM instances whose XY position falls in this cell.
    // Using TArray here is acceptable — cells are built once and never mutated.
    TArray<int32> InstanceIndices;
};


// Uniform 2D grid (XY plane) over HISM instance positions.
// Z is ignored during cell assignment — proximity checks must still
// validate Z distance if vertical separation is significant.
struct GAMECORE_API FHISMInstanceSpatialGrid
{
public:
    FHISMInstanceSpatialGrid() = default;

    // Build the grid from a HISM component. Reads all instance transforms.
    // CellSize should match UHISMProxyConfig::GridCellSize.
    // Called once at BeginPlay on the server.
    void Build(const UHierarchicalInstancedStaticMeshComponent* HISM, float CellSize);

    // Returns all instance indices whose stored position is within QueryRadius
    // of QueryCenter (XY only — see note on Z above).
    // OutCandidates is NOT cleared before appending — caller is responsible.
    void QueryRadius(const FVector& QueryCenter, float QueryRadius,
                     TArray<int32>& OutCandidates) const;

    // Returns the stored world position for a given instance index.
    // Avoids re-fetching from the HISM at query time.
    FORCEINLINE const FVector& GetInstancePosition(int32 InstanceIndex) const
    {
        return InstancePositions[InstanceIndex];
    }

    int32 GetInstanceCount() const { return InstancePositions.Num(); }
    bool  IsBuilt()          const { return bBuilt; }
    void  Reset();  // Clears all data; grid becomes unbuilt.

private:
    // Flat array of instance world positions, indexed by HISM instance index.
    // Stored here so QueryRadius callers never need to touch the HISM component.
    TArray<FVector> InstancePositions;

    // The grid cells. Indexed by GetCellIndex(X, Y).
    TArray<FHISMSpatialCell> Cells;

    FVector  GridOrigin  = FVector::ZeroVector; // world position of cell (0,0)
    float    CellSize    = 0.f;
    int32    GridWidth   = 0;  // number of cells along X
    int32    GridHeight  = 0;  // number of cells along Y
    bool     bBuilt      = false;

    // Converts a world XY position to a grid cell coordinate pair.
    void  WorldToCell(const FVector& WorldPos, int32& OutCellX, int32& OutCellY) const;

    // Converts a cell coordinate pair to a flat array index.
    // Returns INDEX_NONE if the coordinate is outside the grid.
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

    // --- Pass 1: collect all instance world positions and compute AABB ---
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

    // --- Compute grid dimensions from AABB ---
    // Expand bounds by one cell in each direction to avoid edge clamping.
    const FVector BoundsMin = Bounds.Min - CellSize;
    const FVector BoundsMax = Bounds.Max + CellSize;
    GridOrigin = BoundsMin;

    GridWidth  = FMath::CeilToInt((BoundsMax.X - BoundsMin.X) / CellSize);
    GridHeight = FMath::CeilToInt((BoundsMax.Y - BoundsMin.Y) / CellSize);

    Cells.SetNum(GridWidth * GridHeight); // default-constructed, empty arrays

    // --- Pass 2: insert each instance into its cell ---
    for (int32 i = 0; i < NumInstances; ++i)
    {
        int32 CX, CY;
        WorldToCell(InstancePositions[i], CX, CY);
        const int32 CellIdx = GetCellIndex(CX, CY);
        if (CellIdx != INDEX_NONE)
        {
            Cells[CellIdx].InstanceIndices.Add(i);
        }
    }

    bBuilt = true;

    UE_LOG(LogGameCore, Verbose,
        TEXT("FHISMInstanceSpatialGrid: built %d instances into %dx%d cells (%.0fcm each)"),
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

    // Convert radius to cell range — expand by 1 to avoid misses on cell borders.
    const float RadiusSq = QueryRadius * QueryRadius;
    const int32 CellSpan = FMath::CeilToInt(QueryRadius / CellSize) + 1;

    int32 CenterCX, CenterCY;
    WorldToCell(QueryCenter, CenterCX, CenterCY);

    // Iterate the rectangular cell range covering the query circle.
    for (int32 DY = -CellSpan; DY <= CellSpan; ++DY)
    {
        for (int32 DX = -CellSpan; DX <= CellSpan; ++DX)
        {
            const int32 CellIdx = GetCellIndex(CenterCX + DX, CenterCY + DY);
            if (CellIdx == INDEX_NONE) { continue; }

            for (int32 InstanceIdx : Cells[CellIdx].InstanceIndices)
            {
                // XY distance check — callers must validate Z separately.
                const FVector& InstancePos = InstancePositions[InstanceIdx];
                const float DX2 = InstancePos.X - QueryCenter.X;
                const float DY2 = InstancePos.Y - QueryCenter.Y;
                if ((DX2 * DX2 + DY2 * DY2) <= RadiusSq)
                {
                    OutCandidates.Add(InstanceIdx);
                }
            }
        }
    }
}
```

**Why XY only?** HISM instances for world props (trees, rocks, barrels) are generally flat relative to activation radius. A 1500cm activation radius with 200–300cm vertical variance means the XY circle check is sufficient. If your HISM spans large vertical distances (e.g. cliff-face instances), add a Z range check after the XY filter inside `QueryRadius` or in the bridge's per-instance validation loop.

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
    {
        return INDEX_NONE;
    }
    return CellY * GridWidth + CellX;
}
```

---

## Memory Estimate

For 1,000 static instances with `GridCellSize = 1500cm` over a 500m × 500m area:

- Grid dimensions: ~34 × 34 = ~1,156 cells
- `InstancePositions`: 1,000 × 12 bytes = **~12 KB**
- `Cells` array overhead: 1,156 × `TArray` header (~24 bytes) = **~28 KB**
- `InstanceIndices` data: 1,000 × 4 bytes = **~4 KB**
- **Total: ~44 KB** — well within any budget.

---

## Notes

- The grid is **2D (XY)**. It does not handle vertical axis queries natively. This is intentional and correct for typical open-world terrain.
- `QueryRadius` returns candidates — they must still be validated by the bridge against 3D distance if Z variance matters for a specific HISM.
- The grid is built on the **server only**. Clients have no knowledge of it.
- The grid is built **once** at `BeginPlay`. There is no partial-rebuild API because static instances never move. If that assumption breaks, call `Reset()` followed by `Build()` — the full rebuild is O(N) and fast for typical instance counts.
- Do not store a pointer to the `HISM` component inside the grid. The grid stores a copy of positions so the HISM component does not need to be touched after build time.
