#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "ZoneSubsystem.generated.h"

class AZoneActor;

/**
 * UZoneSubsystem
 *
 * Owns the spatial index and provides all zone query APIs.
 * Runs identically on server and client — no authority semantics.
 *
 * Spatial index: 2D uniform grid (default 5000 UU cells).
 * Correct trade-off for 50–500 static zones.
 *
 * Thread safety: all queries are game-thread only.
 */
UCLASS()
class GAMECORE_API UZoneSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // ---- Registration (called by AZoneActor) ----

    void RegisterZone(AZoneActor* Zone);
    void UnregisterZone(AZoneActor* Zone);

    // ---- Spatial Query ----

    /** All zones whose shape contains WorldPoint, sorted by Priority descending. */
    UFUNCTION(BlueprintCallable, Category="Zone")
    TArray<AZoneActor*> QueryZonesAtPoint(const FVector& WorldPoint) const;

    /** The single highest-priority zone at WorldPoint, or nullptr. */
    UFUNCTION(BlueprintCallable, Category="Zone")
    AZoneActor* QueryTopZoneAtPoint(const FVector& WorldPoint) const;

    // ---- Non-Spatial Queries (full scan — use for setup/UI, not per-frame) ----

    /** All zones matching a type tag. */
    UFUNCTION(BlueprintCallable, Category="Zone")
    TArray<AZoneActor*> GetZonesByType(FGameplayTag TypeTag) const;

    /** Direct zone lookup by name tag. Returns first match. */
    UFUNCTION(BlueprintCallable, Category="Zone")
    AZoneActor* GetZoneByName(FGameplayTag NameTag) const;

    // ---- Config ----

    /** Cell size in Unreal Units. Set to roughly the median zone diameter. */
    UPROPERTY(Config)
    float GridCellSize = 5000.f;

private:
    /** Flat map from 2D grid cell → zone list. */
    TMap<FIntPoint, TArray<TWeakObjectPtr<AZoneActor>>> Grid;

    /** All registered zones (for non-spatial queries). */
    TArray<TWeakObjectPtr<AZoneActor>> AllZones;

    FIntPoint             WorldToCell(const FVector& WorldPos) const;
    TArray<FIntPoint>     GetCellsForBounds(const FBox& Bounds) const;
};
