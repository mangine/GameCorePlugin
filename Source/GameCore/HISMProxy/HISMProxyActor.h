// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HISMProxyActor.generated.h"

/**
 * AHISMProxyActor
 *
 * Minimal AActor base class for all proxy actors managed by
 * UHISMProxyBridgeComponent. Deliberately thin — all gameplay behaviour lives
 * in Blueprint subclasses. Defines the activation/deactivation contract and
 * exposes the bound instance index.
 *
 * Marked Abstract to prevent direct placement or assignment to ProxyClass.
 * Only concrete Blueprint subclasses are valid.
 *
 * Server-only management: pool management, proximity, and lifecycle run on the
 * server. Clients receive state via standard Actor replication.
 */
UCLASS(Abstract, Blueprintable)
class GAMECORE_API AHISMProxyActor : public AActor
{
    GENERATED_BODY()
public:
    AHISMProxyActor();

    // ── State ──────────────────────────────────────────────────────────────

    /**
     * The HISM instance index this proxy currently represents.
     * INDEX_NONE when pooled (inactive). Set before OnProxyActivated fires.
     */
    UPROPERTY(BlueprintReadOnly, Category = "HISM Proxy")
    int32 BoundInstanceIndex = INDEX_NONE;

    // ── Lifecycle — called by UHISMProxyBridgeComponent ───────────────────

    /**
     * Called server-side after the proxy is positioned and made visible.
     * Transform is already applied — GetActorTransform() returns the correct
     * world transform inside BP_OnProxyActivated.
     * C++ subclasses: override this virtual.
     * Blueprint subclasses: implement BP_OnProxyActivated.
     */
    virtual void OnProxyActivated(int32 InstanceIndex, const FTransform& InstanceTransform);

    /**
     * Called server-side just before the proxy is hidden and returned to pool.
     * C++ subclasses: override this virtual.
     * Blueprint subclasses: implement BP_OnProxyDeactivated.
     */
    virtual void OnProxyDeactivated();

protected:
    UFUNCTION(BlueprintImplementableEvent, Category = "HISM Proxy",
              meta = (DisplayName = "On Proxy Activated"))
    void BP_OnProxyActivated(int32 InstanceIndex);

    UFUNCTION(BlueprintImplementableEvent, Category = "HISM Proxy",
              meta = (DisplayName = "On Proxy Deactivated"))
    void BP_OnProxyDeactivated();
};
