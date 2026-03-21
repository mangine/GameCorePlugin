# AHISMProxyActor

**File:** `GameCore/Source/GameCore/Public/HISMProxy/HISMProxyActor.h / .cpp`  
**Module:** `GameCore`

Minimal `AActor` base class for all proxy actors managed by `UHISMProxyBridgeComponent`. Deliberately thin — all gameplay behaviour lives in Blueprint subclasses. Defines the activation/deactivation contract and exposes the bound instance index.

Marked `Abstract` to prevent direct placement or assignment to `ProxyClass`. Only concrete Blueprint subclasses are valid.

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
    // INDEX_NONE when pooled (inactive). Set before OnProxyActivated fires.
    UPROPERTY(BlueprintReadOnly, Category = "HISM Proxy")
    int32 BoundInstanceIndex = INDEX_NONE;

    // ── Lifecycle — called by UHISMProxyBridgeComponent ───────────────────

    // Called server-side after the proxy is positioned and made visible.
    // Transform is already applied — GetActorTransform() returns the correct
    // world transform inside BP_OnProxyActivated.
    // C++ subclasses: override this virtual.
    // Blueprint subclasses: implement BP_OnProxyActivated.
    virtual void OnProxyActivated(int32 InstanceIndex, const FTransform& InstanceTransform);

    // Called server-side just before the proxy is hidden and returned to pool.
    // C++ subclasses: override this virtual.
    // Blueprint subclasses: implement BP_OnProxyDeactivated.
    virtual void OnProxyDeactivated();

protected:
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
```

---

## Blueprint Subclass Pattern

**Blueprint class:** `BP_OakTreeProxy` — parent: `AHISMProxyActor`

**Components added in Blueprint:**
- `UInteractionComponent` — with a Harvest interaction entry
- `UStaticMeshComponent` *(optional)* — only if the proxy needs different mesh/LOD

**Event: `BP_OnProxyActivated(InstanceIndex)`**
```
Get HarvestSubsystem
  → QueryInstanceState(BoundInstanceIndex)
    → Available: InteractionComp.SetEntryServerEnabled(0, true)
                 Bind InteractionComp.OnInteractionExecuted → OnHarvestInteracted
    → Harvested:  InteractionComp.SetEntryServerEnabled(0, false)
```

**Event: `BP_OnProxyDeactivated`**
```
InteractionComp.OnInteractionExecuted.Clear()
InteractionComp.SetEntryServerEnabled(0, false)
Flush any partial harvest progress to HarvestSubsystem
Clear any countdown timers started from this proxy
```

**Function: `OnHarvestInteracted(Instigator, EntryIndex)`**
```
HarvestSubsystem.HarvestTree(BoundInstanceIndex, Instigator)
InteractionComp.SetEntryServerEnabled(0, false)
```

---

## Notes

- **`Abstract` prevents direct use.** `ValidateSetup` on `AHISMProxyHostActor` verifies `ProxyClass != AHISMProxyActor::StaticClass()` at editor time.
- **Replication is on by default.** Clients see proxies via standard Actor relevancy. No custom replication code.
- **Collision managed by the bridge.** Do not call `SetActorEnableCollision` from `BeginPlay` inside the proxy Blueprint — it will be overridden by the bridge. Configure collision channel responses in component defaults.
- **No state persists across pool cycles.** `OnProxyActivated` must fully re-initialise the actor. The proxy may have been used for a completely different `InstanceIndex` previously.
- **Transform set before `OnProxyActivated`.** `GetActorTransform()` returns the correct instance world transform inside `BP_OnProxyActivated`.
- **Tick off by default.** Ticking many pooled actors at MMO densities carries real CPU cost. Enable tick selectively in subclass Blueprints only when genuinely needed.
