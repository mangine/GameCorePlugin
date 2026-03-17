# AHISMProxyActor

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`AHISMProxyActor` is the **minimal base class** for all proxy actors managed by `UHISMProxyBridgeComponent`. It is deliberately thin — almost all gameplay behaviour lives in Blueprint subclasses. The base class defines the activation/deactivation contract and exposes the bound instance index.

**Files:** `HISMProxy/HISMProxyActor.h / .cpp`

---

## Design Intent

Proxy actors are standard `AActor` subclasses. They replicate to clients via UE's Actor relevancy exactly like any hand-placed world Actor. They can carry `UInteractionComponent`, `UStaticMeshComponent`, GAS components, or anything else an `AActor` can host.

The pool pre-allocates them at `BeginPlay` spawned hidden near the host actor below terrain. Activation is a transform set + visibility toggle — not a `SpawnActor` call.

> **`Abstract` note:** The base class is marked `Abstract` to prevent it from being used directly in `ProxyClass` or placed in the world. Only concrete Blueprint subclasses are valid. `UHISMProxyBridgeComponent::SpawnPoolActor` calls `SpawnActor` with the configured `ProxyClass` (a concrete subclass) — it never passes the base class. `ValidateSetup` checks this at editor time.

---

## Class Definition

```cpp
UCLASS(Abstract, Blueprintable)
class GAMECORE_API AHISMProxyActor : public AActor
{
    GENERATED_BODY()
public:
    AHISMProxyActor();

    // ── State ──────────────────────────────────────────────────────────────

    // The HISM instance index this proxy currently represents.
    // INDEX_NONE when the proxy is in the pool (inactive).
    UPROPERTY(BlueprintReadOnly, Category = "HISM Proxy")
    int32 BoundInstanceIndex = INDEX_NONE;

    // ── Lifecycle Hooks — called by UHISMProxyBridgeComponent ────────────────

    // Called on the server after the proxy is positioned and made visible.
    // Subclasses initialise gameplay state here: query resource data,
    // configure InteractionComponent entries, bind delegates, etc.
    virtual void OnProxyActivated(int32 InstanceIndex, const FTransform& InstanceTransform);

    // Called on the server just before the proxy is hidden and returned to pool.
    // Subclasses must clean up here: unbind delegates, flush partial state
    // to external game system storage, clear timers.
    virtual void OnProxyDeactivated();

protected:
    // Blueprint-implementable mirrors of the C++ virtuals.
    // C++ subclasses: override the virtual. Blueprint subclasses: implement these events.
    UFUNCTION(BlueprintImplementableEvent, Category = "HISM Proxy",
              meta = (DisplayName = "On Proxy Activated"))
    void BP_OnProxyActivated(int32 InstanceIndex);

    UFUNCTION(BlueprintImplementableEvent, Category = "HISM Proxy",
              meta = (DisplayName = "On Proxy Deactivated"))
    void BP_OnProxyDeactivated();
};
```

---

## Implementation

```cpp
AHISMProxyActor::AHISMProxyActor()
{
    // Proxies replicate to clients via standard Actor relevancy.
    bReplicates = true;

    // Tick is off on the base class. Enable in Blueprint subclasses only if needed.
    // Be aware that many ticking proxies is a CPU cost at MMO densities.
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
```

---

## Blueprint Subclass Pattern

**Blueprint class:** `BP_OakTreeProxy` — parent: `AHISMProxyActor`

**Components added in the Blueprint:**
- `UInteractionComponent` with a Harvest entry
- `UStaticMeshComponent` (optional — only if the proxy needs a different mesh or LOD than the HISM)

**Event: `On Proxy Activated(InstanceIndex)`**
```
Get HarvestSubsystem
  → QueryInstanceState(InstanceIndex)
    → Harvested:  InteractionComp.SetEntryServerEnabled(0, false)
    → Available:  InteractionComp.SetEntryServerEnabled(0, true)
                  Bind InteractionComp.OnInteractionExecuted → OnHarvestInteracted
```

**Event: `On Proxy Deactivated`**
```
Unbind all delegates on InteractionComp
Clear any active timers (cooldown countdowns, etc.)
Flush any partial interaction state to HarvestSubsystem storage
```

**Function: `OnHarvestInteracted(Instigator, EntryIndex)`**
```
HarvestSubsystem.HarvestTree(BoundInstanceIndex, Instigator)
InteractionComp.SetEntryServerEnabled(0, false)
```

---

## Notes

- **`Abstract` prevents direct use.** Only concrete Blueprint subclasses can be assigned to `FHISMProxyInstanceType::ProxyClass`. `ValidateSetup` in `AHISMProxyHostActor` checks that `ProxyClass` is not the base class itself and is not null.
- **Replication is on by default.** Clients see proxies appear and disappear through standard Actor relevancy. No custom replication code needed.
- **Collision is managed by the bridge.** The bridge calls `SetActorEnableCollision(true/false)` on activation/deactivation. Do not call `SetActorEnableCollision` from `BeginPlay` inside the proxy Blueprint — it will be overridden. Configure collision channel responses in component defaults.
- **No state persists across pool cycles.** `OnProxyActivated` must fully re-initialise the actor for the new `InstanceIndex`. The proxy may have been used for a completely different instance previously.
- **Transform is set before `OnProxyActivated`.** `GetActorTransform()` returns the correct world transform inside `BP_OnProxyActivated`.
- **C++ subclasses** override the virtual methods. Blueprint subclasses implement the `BP_` events. Both approaches work; both call the same bridge contract.
