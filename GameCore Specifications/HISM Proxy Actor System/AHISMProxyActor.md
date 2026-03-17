# AHISMProxyActor

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`AHISMProxyActor` is the **minimal base class** for all proxy actors managed by `UHISMProxyBridgeComponent`. It is deliberately thin — almost all gameplay behaviour lives in Blueprint subclasses. The base class exists only to define the activation/deactivation contract and expose the bound instance index.

**Files:** `HISMProxy/HISMProxyActor.h / .cpp`

---

## Design Intent

Proxy actors are **standard AActor subclasses**. They replicate to clients via UE's Actor relevancy exactly like any hand-placed world Actor. They can carry `UInteractionComponent`, `UStaticMeshComponent`, GAS components, or anything else an AActor can host. The system imposes no restrictions.

The pool pre-allocates them at `BeginPlay` spawned hidden at the world origin. Activation is a transform set + visibility toggle — not a `SpawnActor` call. The base class's virtual hooks let game code react to these lifecycle events without subclassing the bridge component.

---

## Class Definition

```cpp
UCLASS(Abstract, Blueprintable)
class GAMECORE_API AHISMProxyActor : public AActor
{
    GENERATED_BODY()
public:
    AHISMProxyActor();

    // ── State ─────────────────────────────────────────────────────────────────

    // The HISM instance index this proxy currently represents.
    // INDEX_NONE when the proxy is in the pool (inactive).
    UPROPERTY(BlueprintReadOnly, Category = "HISM Proxy")
    int32 BoundInstanceIndex = INDEX_NONE;

    // ── Lifecycle Hooks — called by UHISMProxyBridgeComponent ────────────────

    // Called on the server when the proxy is pulled from the pool and assigned
    // to InstanceIndex. Transform has already been set before this call.
    // Subclasses should initialise gameplay state here (load resource data,
    // configure InteractionComponent entries, set mesh, etc.).
    virtual void OnProxyActivated(int32 InstanceIndex, const FTransform& InstanceTransform);

    // Called on the server just before the proxy is hidden and returned to pool.
    // Subclasses should clean up any transient state here.
    virtual void OnProxyDeactivated();

protected:
    // Blueprint-implementable events mirror the C++ virtuals.
    // C++ subclasses override the virtual; Blueprint subclasses override these.
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
    // Proxies replicate like normal Actors — clients see them via relevancy.
    bReplicates = true;

    // No per-frame tick needed on the base class.
    // Subclasses may enable tick if required.
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

This is the standard pattern for a game-specific proxy actor. The example shows an oak tree harvesting proxy.

**Blueprint class:** `BP_OakTreeProxy` (parent: `AHISMProxyActor`)

**Components added in the Blueprint:**
- `UInteractionComponent` — with a `Harvest` entry
- `UStaticMeshComponent` (optional) — only if a different LOD or material is needed on the proxy vs. the HISM mesh

**Event Graph — `On Proxy Activated`:**

```
Event BP_OnProxyActivated (InstanceIndex)
  |
  v
  Get Game Instance Subsystem (UHarvestSubsystem)
  |
  v
  Query resource state for InstanceIndex
  |
  +-- Is on cooldown? --> SetEntryServerEnabled(HarvestEntry, false)
  |
  +-- Is available?  --> SetEntryServerEnabled(HarvestEntry, true)
                          Bind OnInteractionExecuted → Harvest_OnInteract
```

**Event Graph — `On Proxy Deactivated`:**

```
Event BP_OnProxyDeactivated
  |
  v
  Unbind OnInteractionExecuted delegates
  Clear any running timers (e.g. respawn countdown UI)
```

**Event Graph — `Harvest_OnInteract` (bound in OnProxyActivated):**

```
Harvest_OnInteract (Instigator, EntryIndex)
  |
  v
  Notify UHarvestSubsystem → HarvestInstance(BoundInstanceIndex, Instigator)
  |
  v
  SetEntryServerEnabled(HarvestEntry, false)  [disable until respawn]
```

---

## Notes

- **`Abstract` + `Blueprintable`**: the base class cannot be placed or assigned directly in a config asset — only concrete Blueprint subclasses can. This prevents accidentally assigning the raw base class in `UHISMProxyConfig::ProxyClasses`.
- **Replication**: `bReplicates = true` by default. Clients will see the proxy appear and disappear through UE's standard Actor relevancy and `SetActorHiddenInGame`. No custom replication is needed.
- **Collision**: the bridge enables/disables collision via `SetActorEnableCollision`. Subclasses must not override this in `BeginPlay` — configure collision channel responses on components in the Blueprint defaults, and let the bridge manage the enabled state.
- **No state should persist across activations.** `OnProxyActivated` must fully reinitialise the actor for the new `InstanceIndex`. Do not assume any state from the previous activation persists.
- **Transform is set before `OnProxyActivated` is called.** Subclasses may read `GetActorTransform()` normally inside `BP_OnProxyActivated`.
- **Tick is off by default.** Enable it in the Blueprint if needed (e.g. for a respawn countdown). Be mindful that many active proxies with per-frame tick is a CPU concern at MMO densities.
- **C++ game systems** that need more control can subclass `AHISMProxyActor` in C++ and override the virtual methods instead of using Blueprint events. Both approaches are fully supported.
