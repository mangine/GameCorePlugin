// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UHierarchicalInstancedStaticMeshComponent;

// ─────────────────────────────────────────────────────────────────────────────
// FHISMSpatialCell
// ─────────────────────────────────────────────────────────────────────────────

/** A single cell in the FHISMInstanceSpatialGrid. */
struct FHISMSpatialCell
{
    /** Instance indices whose XY position falls in this cell. */
    TArray<int32> InstanceIndices;
};

// ─────────────────────────────────────────────────────────────────────────────
// FHISMInstanceSpatialGrid
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Lightweight, server-only uniform 2D spatial grid (XY plane) built once over
 * all HISM instance positions at BeginPlay.
 *
 * Enables O(cell-count) candidate lookup for a world position and radius —
 * avoids a full O(N) linear scan of all instances every proximity tick.
 *
 * Plain C++ struct — no UObject overhead, no GC involvement.
 */
struct GAMECORE_API FHISMInstanceSpatialGrid
{
public:
    FHISMInstanceSpatialGrid() = default;

    /**
     * Build from a HISM component. Reads all instance world transforms.
     * CellSize should match UHISMProxyConfig::GridCellSize.
     * Call once at BeginPlay on the server.
     */
    void Build(const UHierarchicalInstancedStaticMeshComponent* HISM, float CellSize);

    /**
     * Appends to OutCandidates all instance indices within QueryRadius of
     * QueryCenter (XY). OutCandidates is NOT cleared — caller is responsible.
     */
    void QueryRadius(const FVector& QueryCenter, float QueryRadius,
                     TArray<int32>& OutCandidates) const;

    /** Returns stored world position for InstanceIndex. */
    FORCEINLINE const FVector& GetInstancePosition(int32 InstanceIndex) const
    {
        return InstancePositions[InstanceIndex];
    }

    int32 GetInstanceCount() const { return InstancePositions.Num(); }
    bool  IsBuilt()          const { return bBuilt; }

    /** Clears all data; grid becomes unbuilt. */
    void Reset();

private:
    /** World positions indexed by HISM instance index. */
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
