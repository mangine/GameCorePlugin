# Zone Subsystem

`UZoneSubsystem` is a `UWorldSubsystem` that owns the spatial index and provides all query APIs. It has no authority — it runs identically on server and client.

---

## Uniform Grid Index

At 50–500 static zones a 2D uniform grid is the right trade-off: `O(1)` cell lookup, trivial memory cost, zero maintenance overhead for static zones.

- Grid lives in world XY space.
- Each cell stores a list of zone actors whose AABB overlaps that cell.
- Query: hash the point to a cell, then exact-test each candidate with `ContainsPoint()`.
- Cell size is configurable (default 5000 UU ≈ 50 m). Set to roughly the median zone diameter.

---

## Class Definition

```cpp
UCLASS()
class GAMECORE_API UZoneSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // ---- Registration (called by AZoneActor) ----
    void RegisterZone(AZoneActor* Zone);
    void UnregisterZone(AZoneActor* Zone);

    // ---- Query API ----

    // All zones whose shape contains WorldPoint, sorted by Priority (descending).
    UFUNCTION(BlueprintCallable, Category="Zone")
    TArray<AZoneActor*> QueryZonesAtPoint(const FVector& WorldPoint) const;

    // The single highest-priority zone at WorldPoint, or nullptr.
    UFUNCTION(BlueprintCallable, Category="Zone")
    AZoneActor* QueryTopZoneAtPoint(const FVector& WorldPoint) const;

    // All zones matching a type tag (no spatial filter — use for global lookups).
    UFUNCTION(BlueprintCallable, Category="Zone")
    TArray<AZoneActor*> GetZonesByType(FGameplayTag TypeTag) const;

    // Direct zone lookup by name tag.
    UFUNCTION(BlueprintCallable, Category="Zone")
    AZoneActor* GetZoneByName(FGameplayTag NameTag) const;

    // ---- Config ----
    UPROPERTY(Config)
    float GridCellSize = 5000.f;

private:
    // Grid storage: flat map from cell key to zone list
    TMap<FIntPoint, TArray<TWeakObjectPtr<AZoneActor>>> Grid;

    // All registered zones (for non-spatial queries)
    TArray<TWeakObjectPtr<AZoneActor>> AllZones;

    FIntPoint WorldToCell(const FVector& WorldPos) const;
    TArray<FIntPoint> GetCellsForBounds(const FBox& Bounds) const;
};
```

---

## Key Method Implementations

### `RegisterZone`

```cpp
void UZoneSubsystem::RegisterZone(AZoneActor* Zone)
{
    if (!Zone || !Zone->DataAsset) return;

    AllZones.AddUnique(Zone);

    const FBox Bounds = Zone->GetWorldBounds();
    for (const FIntPoint& Cell : GetCellsForBounds(Bounds))
        Grid.FindOrAdd(Cell).AddUnique(Zone);
}
```

### `UnregisterZone`

```cpp
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
```

### `QueryZonesAtPoint`

```cpp
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

    // Sort by Priority descending
    Result.Sort([](const AZoneActor& A, const AZoneActor& B)
    {
        const int32 PA = A.DataAsset ? A.DataAsset->Priority : 0;
        const int32 PB = B.DataAsset ? B.DataAsset->Priority : 0;
        return PA > PB;
    });

    return Result;
}
```

### `WorldToCell` / `GetCellsForBounds`

```cpp
FIntPoint UZoneSubsystem::WorldToCell(const FVector& WorldPos) const
{
    return FIntPoint(
        FMath::FloorToInt(WorldPos.X / GridCellSize),
        FMath::FloorToInt(WorldPos.Y / GridCellSize)
    );
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
```

> **Note:** Zones spanning multiple cells are stored in every overlapping cell. At the target scale this results in negligible memory duplication. A zone 10 km wide with 5 km cells spans at most 4 cells in each axis — still manageable.

> **Thread safety:** All queries are game-thread only. No locking required.
