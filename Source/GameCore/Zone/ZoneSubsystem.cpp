#include "ZoneSubsystem.h"
#include "Zone/ZoneActor.h"
#include "Zone/ZoneDataAsset.h"

// =============================================================================
// Registration
// =============================================================================

void UZoneSubsystem::RegisterZone(AZoneActor* Zone)
{
    if (!Zone || !Zone->DataAsset) return;

    AllZones.AddUnique(Zone);

    const FBox Bounds = Zone->GetWorldBounds();
    for (const FIntPoint& Cell : GetCellsForBounds(Bounds))
        Grid.FindOrAdd(Cell).AddUnique(Zone);
}

void UZoneSubsystem::UnregisterZone(AZoneActor* Zone)
{
    AllZones.Remove(Zone);

    const FBox Bounds = Zone->GetWorldBounds();
    for (const FIntPoint& Cell : GetCellsForBounds(Bounds))
    {
        if (TArray<TWeakObjectPtr<AZoneActor>>* List = Grid.Find(Cell))
            List->Remove(Zone);
    }
}

// =============================================================================
// Spatial Query
// =============================================================================

TArray<AZoneActor*> UZoneSubsystem::QueryZonesAtPoint(const FVector& WorldPoint) const
{
    TArray<AZoneActor*> Result;
    const FIntPoint Cell = WorldToCell(WorldPoint);

    const TArray<TWeakObjectPtr<AZoneActor>>* List = Grid.Find(Cell);
    if (!List) return Result;

    for (const TWeakObjectPtr<AZoneActor>& Weak : *List)
    {
        if (AZoneActor* Zone = Weak.Get())
        {
            if (Zone->ContainsPoint(WorldPoint))
                Result.Add(Zone);
        }
    }

    Result.Sort([](const AZoneActor& A, const AZoneActor& B)
    {
        const int32 PA = A.DataAsset ? A.DataAsset->Priority : 0;
        const int32 PB = B.DataAsset ? B.DataAsset->Priority : 0;
        return PA > PB;
    });

    return Result;
}

AZoneActor* UZoneSubsystem::QueryTopZoneAtPoint(const FVector& WorldPoint) const
{
    TArray<AZoneActor*> Zones = QueryZonesAtPoint(WorldPoint);
    return Zones.Num() > 0 ? Zones[0] : nullptr;
}

// =============================================================================
// Non-Spatial Queries
// =============================================================================

TArray<AZoneActor*> UZoneSubsystem::GetZonesByType(FGameplayTag TypeTag) const
{
    TArray<AZoneActor*> Result;
    for (const TWeakObjectPtr<AZoneActor>& Weak : AllZones)
    {
        if (AZoneActor* Zone = Weak.Get())
        {
            if (Zone->DataAsset &&
                Zone->DataAsset->ZoneTypeTag.MatchesTagExact(TypeTag))
                Result.Add(Zone);
        }
    }
    return Result;
}

AZoneActor* UZoneSubsystem::GetZoneByName(FGameplayTag NameTag) const
{
    for (const TWeakObjectPtr<AZoneActor>& Weak : AllZones)
    {
        if (AZoneActor* Zone = Weak.Get())
        {
            if (Zone->DataAsset &&
                Zone->DataAsset->ZoneNameTag.MatchesTagExact(NameTag))
                return Zone;
        }
    }
    return nullptr;
}

// =============================================================================
// Grid Helpers
// =============================================================================

FIntPoint UZoneSubsystem::WorldToCell(const FVector& WorldPos) const
{
    return FIntPoint(
        FMath::FloorToInt(WorldPos.X / GridCellSize),
        FMath::FloorToInt(WorldPos.Y / GridCellSize));
}

TArray<FIntPoint> UZoneSubsystem::GetCellsForBounds(const FBox& Bounds) const
{
    TArray<FIntPoint> Cells;
    const FIntPoint Min = WorldToCell(Bounds.Min);
    const FIntPoint Max = WorldToCell(Bounds.Max);
    for (int32 X = Min.X; X <= Max.X; ++X)
        for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
            Cells.Add(FIntPoint(X, Y));
    return Cells;
}
