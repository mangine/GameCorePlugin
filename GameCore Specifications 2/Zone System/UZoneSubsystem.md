# UZoneSubsystem

**File:** `GameCore/Source/GameCore/Zone/ZoneSubsystem.h` / `.cpp`

`UZoneSubsystem` is a `UWorldSubsystem` that owns the spatial index and provides all zone query APIs. It runs identically on server and client — it has no authority semantics.

---

## Uniform Grid Index

At 50–500 static zones a 2D uniform grid is the correct trade-off:
- `O(1)` cell lookup by hashing the query point
- Trivial memory cost; no tree maintenance
- Multi-cell registration for zones spanning multiple cells (negligible duplication at this scale)

Cell size is configurable (default `5000 UU ≈ 50 m`). Set to roughly the median zone diameter.

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

    // ---- Spatial Query ----

    // All zones whose shape contains WorldPoint, sorted by Priority descending.
    UFUNCTION(BlueprintCallable, Category="Zone")
    TArray<AZoneActor*> QueryZonesAtPoint(const FVector& WorldPoint) const;

    // The single highest-priority zone at WorldPoint, or nullptr.
    UFUNCTION(BlueprintCallable, Category="Zone")
    AZoneActor* QueryTopZoneAtPoint(const FVector& WorldPoint) const;

    // ---- Non-Spatial Queries ----

    // All zones matching a type tag (full scan — use for setup/UI, not per-frame).
    UFUNCTION(BlueprintCallable, Category="Zone")
    TArray<AZoneActor*> GetZonesByType(FGameplayTag TypeTag) const;

    // Direct zone lookup by name tag. Returns first match.
    UFUNCTION(BlueprintCallable, Category="Zone")
    AZoneActor* GetZoneByName(FGameplayTag NameTag) const;

    // ---- Config ----

    UPROPERTY(Config)
    float GridCellSize = 5000.f;

private:
    // Grid: flat map from cell key → zone list
    TMap<FIntPoint, TArray<TWeakObjectPtr<AZoneActor>>> Grid;

    // All registered zones (for non-spatial queries)
    TArray<TWeakObjectPtr<AZoneActor>> AllZones;

    FIntPoint WorldToCell(const FVector& WorldPos) const;
    TArray<FIntPoint> GetCellsForBounds(const FBox& Bounds) const;
};
```

---

## Implementations

### `RegisterZone` / `UnregisterZone`

```cpp
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

### `QueryTopZoneAtPoint`

```cpp
AZoneActor* UZoneSubsystem::QueryTopZoneAtPoint(const FVector& WorldPoint) const
{
    TArray<AZoneActor*> Zones = QueryZonesAtPoint(WorldPoint);
    return Zones.Num() > 0 ? Zones[0] : nullptr;
}
```

### `GetZonesByType`

```cpp
TArray<AZoneActor*> UZoneSubsystem::GetZonesByType(FGameplayTag TypeTag) const
{
    TArray<AZoneActor*> Result;
    for (const TWeakObjectPtr<AZoneActor>& Weak : AllZones)
    {
        if (AZoneActor* Zone = Weak.Get())
        {
            if (Zone->DataAsset && Zone->DataAsset->ZoneTypeTag.MatchesTagExact(TypeTag))
                Result.Add(Zone);
        }
    }
    return Result;
}
```

### `GetZoneByName`

```cpp
AZoneActor* UZoneSubsystem::GetZoneByName(FGameplayTag NameTag) const
{
    for (const TWeakObjectPtr<AZoneActor>& Weak : AllZones)
    {
        if (AZoneActor* Zone = Weak.Get())
        {
            if (Zone->DataAsset && Zone->DataAsset->ZoneNameTag.MatchesTagExact(NameTag))
                return Zone;
        }
    }
    return nullptr;
}
```

### `WorldToCell` / `GetCellsForBounds`

```cpp
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
```

---

## Notes

- **Thread safety:** All queries are game-thread only. No locking required.
- **Weak pointers:** `TWeakObjectPtr` guards against dangling pointers if a zone is destroyed without calling `UnregisterZone` (e.g. editor undo). The returned `TArray<AZoneActor*>` from queries contains only valid pointers.
- **Non-spatial queries are full scans.** `GetZonesByType` and `GetZoneByName` iterate `AllZones`. At 50–500 zones this is fast. Do not call per-frame; use for initialisation and UI.
- **Zones spanning multiple cells** are stored in every overlapping cell. A 10 km zone with 5 km cells spans at most 4 cells per axis — negligible duplication.
