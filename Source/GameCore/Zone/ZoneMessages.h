#pragma once

#include "CoreMinimal.h"
#include "Zone/ZoneTypes.h"
#include "ZoneMessages.generated.h"

class AZoneActor;
class UZoneDataAsset;

/**
 * Broadcast on GameCore.Zone.Channel.Transition by UZoneTrackerComponent.
 * Scope: Both (server and client broadcast independently).
 */
USTRUCT(BlueprintType)
struct FZoneTransitionMessage
{
    GENERATED_BODY()

    /** The actor that crossed the zone boundary. */
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> TrackedActor;

    /** The zone being entered or exited. */
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AZoneActor> ZoneActor;

    /** Snapshot of static data at the time of transition. */
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UZoneDataAsset> StaticData;

    /** Snapshot of dynamic state at the time of transition. */
    UPROPERTY(BlueprintReadOnly)
    FZoneDynamicState DynamicState;

    /** True = entered, False = exited. */
    UPROPERTY(BlueprintReadOnly)
    bool bEntered = false;
};

/**
 * Broadcast on GameCore.Zone.Channel.StateChanged by AZoneActor.
 * Server: ServerOnly via SetOwnerTag / AddDynamicTag / RemoveDynamicTag.
 * Clients: ClientOnly via OnRep_DynamicState.
 */
USTRUCT(BlueprintType)
struct FZoneStateChangedMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AZoneActor> ZoneActor;

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UZoneDataAsset> StaticData;

    /** The new dynamic state. */
    UPROPERTY(BlueprintReadOnly)
    FZoneDynamicState DynamicState;
};
