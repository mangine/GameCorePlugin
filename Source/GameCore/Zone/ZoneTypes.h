#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ZoneTypes.generated.h"

UENUM(BlueprintType)
enum class EZoneShapeType : uint8
{
    Box,
    ConvexPolygon
};

/**
 * Serialisable shape descriptor used for runtime zone spawning.
 * Not used for editor-placed zones — those are configured directly on shape components.
 */
USTRUCT(BlueprintType)
struct FZoneShapeData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    EZoneShapeType ShapeType = EZoneShapeType::Box;

    /** Box: half-extents in local space. */
    UPROPERTY(EditAnywhere)
    FVector BoxExtent = FVector(500.f);

    /** ConvexPolygon: XY points in local space (min 3, wound CCW from above). */
    UPROPERTY(EditAnywhere)
    TArray<FVector2D> PolygonPoints;

    /** ConvexPolygon: world-space Z range. */
    UPROPERTY(EditAnywhere)
    float MinZ = -500.f;

    UPROPERTY(EditAnywhere)
    float MaxZ = 500.f;
};

/**
 * Mutable zone state replicated from server to all clients.
 * Kept minimal — only data that actually changes at runtime.
 * Mark containing AZoneActor property with ReplicatedUsing=OnRep_DynamicState.
 */
USTRUCT(BlueprintType)
struct FZoneDynamicState
{
    GENERATED_BODY()

    /** Faction/player/entity that currently controls this zone. */
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag OwnerTag;

    /** Runtime-added tags (buffs, siege status, event states, etc.). */
    UPROPERTY(BlueprintReadOnly)
    FGameplayTagContainer DynamicTags;

    bool operator==(const FZoneDynamicState& Other) const
    {
        return OwnerTag == Other.OwnerTag && DynamicTags == Other.DynamicTags;
    }
};
