// Copyright GameCore Plugin. All Rights Reserved.

#include "HISMProxy/HISMProxyActor.h"

AHISMProxyActor::AHISMProxyActor()
{
    // Replicate to clients via standard Actor relevancy.
    // No custom replication code needed.
    bReplicates = true;

    // Tick off by default — many ticking proxies is expensive at MMO densities.
    // Enable per-Blueprint subclass only if required.
    PrimaryActorTick.bCanEverTick = false;
}

void AHISMProxyActor::OnProxyActivated(
    int32 InstanceIndex, const FTransform& InstanceTransform)
{
    BoundInstanceIndex = InstanceIndex;
    BP_OnProxyActivated(InstanceIndex);
}

void AHISMProxyActor::OnProxyDeactivated()
{
    BoundInstanceIndex = INDEX_NONE;
    BP_OnProxyDeactivated();
}
