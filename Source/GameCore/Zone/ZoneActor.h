#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Zone/ZoneTypes.h"
#include "Zone/ZoneShapeComponent.h"
#include "ZoneActor.generated.h"

class UZoneDataAsset;

/**
 * The replicated world actor that owns one active shape component and mutable FZoneDynamicState.
 * Known to both server and all clients (bAlwaysRelevant = true).
 *
 * - Shape selection: ShapeType controls which of BoxShape / ConvexShape is the active one.
 * - State mutation: SetOwnerTag / AddDynamicTag / RemoveDynamicTag are BlueprintAuthorityOnly.
 *   They immediately broadcast FZoneStateChangedMessage (ServerOnly) and replicate DynamicState
 *   to clients, where OnRep_DynamicState fires FZoneStateChangedMessage (ClientOnly).
 */
UCLASS(BlueprintType)
class GAMECORE_API AZoneActor : public AActor
{
    GENERATED_BODY()

public:
    AZoneActor();

    // ---- Static Data ----

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    TObjectPtr<UZoneDataAsset> DataAsset;

    // ---- Shape ----
    // Both components are always created; only the active one is queried.
    // ShapeType selects which component GetActiveShape() returns.

    UPROPERTY(EditAnywhere, Category="Zone")
    EZoneShapeType ShapeType = EZoneShapeType::Box;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
    TObjectPtr<UZoneBoxShapeComponent> BoxShape;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
    TObjectPtr<UZoneConvexPolygonShapeComponent> ConvexShape;

    // ---- Mutable State (replicated) ----

    UPROPERTY(ReplicatedUsing=OnRep_DynamicState, BlueprintReadOnly, Category="Zone")
    FZoneDynamicState DynamicState;

    // ---- Query API ----

    /** Returns true if WorldPoint is inside this zone. */
    UFUNCTION(BlueprintCallable, Category="Zone")
    bool ContainsPoint(const FVector& WorldPoint) const;

    /** World-space AABB for spatial index registration. */
    FBox GetWorldBounds() const;

    // ---- Mutation API (server only) ----

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Zone")
    void SetOwnerTag(FGameplayTag NewOwner);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Zone")
    void AddDynamicTag(FGameplayTag Tag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Zone")
    void RemoveDynamicTag(FGameplayTag Tag);

    /**
     * Server-only: configure zone after runtime spawn.
     * Must be called before the actor is registered with UZoneSubsystem
     * (i.e. before BeginPlay, or immediately after spawn if BeginPlay already ran).
     */
    void InitializeZone(UZoneDataAsset* InDataAsset, const FZoneShapeData& InShapeData);

    // ---- UE Overrides ----
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutProps) const override;

private:
    UFUNCTION()
    void OnRep_DynamicState();

    UZoneShapeComponent* GetActiveShape() const;
    void BroadcastStateChanged(EGameCoreEventScope Scope);
};
