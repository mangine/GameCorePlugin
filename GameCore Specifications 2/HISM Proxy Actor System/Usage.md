# HISM Proxy Actor System — Usage Guide

**Audience:** Designers and gameplay programmers integrating the system.  
**Prerequisite:** Read `Architecture.md` once to understand the pool/bridge mental model.

---

## Step 1 — Prepare the HISM Material

Every mesh in the system must have a material that discards pixels when `PerInstanceCustomData[0]` is set to `1.0`.

**Create shared Material Function `MF_HISMProxyHide`** at:
`Content/GameCore/Materials/Functions/MF_HISMProxyHide.uasset`

Graph:
```
[PerInstanceCustomData]  Index=0, Default=0.0
      |
[If]  A >= 0.5  →  -1.0
                →   1.0
      |
[Clip]
```

Call this node in every HISM material graph. Verify `bUsedWithInstancedStaticMeshes = true` in Material Settings.

> **Why a function?** Centralises the hide logic. A wrong slot index in a per-material implementation silently breaks instance hiding — the function is correct once and shared everywhere.

---

## Step 2 — Create a `UHISMProxyConfig` Data Asset

Right-click Content Browser → **GameCore → HISM Proxy Config**.

| Property | Recommended | Notes |
|---|---|---|
| `ActivationRadius` | 1500 cm | 15m — player must be within this to activate a proxy |
| `DeactivationRadiusBonus` | 400 cm | Prevents boundary thrashing |
| `DeactivationDelay` | 5.0 s | Proxy stays live after all players leave |
| `ProximityTickInterval` | 0.5 s | Server tick rate for proximity check |
| `GridCellSize` | 1500 cm | Match `ActivationRadius` for optimal grid performance |

One config asset is sufficient for most projects. Create separate assets only when proximity behaviour genuinely differs (e.g. indoor vs outdoor props).

---

## Step 3 — Create Proxy Actor Blueprints

For each mesh type that needs gameplay behaviour:

1. Create Blueprint with parent **`AHISMProxyActor`**
2. Add gameplay components: `UInteractionComponent`, VFX, audio, etc.
3. Override **`BP_OnProxyActivated(InstanceIndex)`**
4. Override **`BP_OnProxyDeactivated`**

```
BP_OnProxyActivated(InstanceIndex):
  → Query your game subsystem using BoundInstanceIndex
  → Configure UInteractionComponent entries (enable/disable based on state)
  → Bind delegates

BP_OnProxyDeactivated:
  → Unbind all delegates
  → Clear timers started from this proxy
  → Flush partial state to your game subsystem
     (e.g. partial harvest progress, partial crafting progress)
```

> **Key rule:** `OnProxyDeactivated` must leave the proxy fully clean. It will be reused for a different `InstanceIndex` on the next activation.

> **Do not call `SetActorEnableCollision` from `BeginPlay`.** The bridge manages collision on/off. Configure collision channel responses in component defaults instead.

---

## Step 4 — Place an `AHISMProxyHostActor`

1. Place Actor → search **HISM Proxy Host Actor**
2. In **Details → HISM Proxy → Instance Types**, click **+** per mesh type
3. Fill each entry:

| Field | Example |
|---|---|
| `TypeName` | `Oak` |
| `Mesh` | `SM_OakTree` |
| `ProxyClass` | `BP_OakTreeProxy` |
| `Config` | `DA_Forest_ProxyConfig` |
| `MinPoolSize` | computed from formula |
| `MaxPoolSize` | theoretical maximum |
| `GrowthBatchSize` | 8 |

After filling each entry, `HISM_Oak` and `Bridge_Oak` are created automatically. No manual component wiring.

### Pool Sizing Formula

```
InstanceDensity = TotalInstances / AreaM²
RadiusM         = ActivationRadius / 100.0
MaxPerPlayer    = PI * RadiusM² * InstanceDensity
MinPoolSize     = ceil(MaxPerPlayer * ExpectedConcurrentPlayers * 1.2)
```

**Example** — 500 oaks / 40,000 m², radius=15m, 64 players:
```cpp
Density      = 500 / 40000 = 0.0125 /m²
MaxPerPlayer = PI * 225 * 0.0125 ≈ 8.8
MinPoolSize  = ceil(8.8 * 64 * 1.2) = 676
```
In practice players cluster. Set `MinPoolSize = 300–400`, `MaxPoolSize = 676`.  
If `"pool exhausted"` warnings appear in logs, raise `MinPoolSize`.

---

## Step 5A — Mass Placement via Foliage Tool (Recommended)

1. Paint instances with the Foliage Tool (density, slope, random scale all supported)
2. Right-click Content Browser → **Editor Utilities → Editor Utility Object**, parent `UHISMFoliageConversionUtility`
3. Set `TargetHostActor` to your host actor
4. Set `bRemoveFoliageAfterConversion = true` (prevents double rendering)
5. Click **Convert Foliage To Proxy Host**

The converter is fully undoable via Ctrl+Z. Per-instance scale is preserved.

> **Sub-levels:** Run the converter once per sub-level while it is the active level. The converter only sees the current persistent level's foliage.

---

## Step 5B — Manual Placement

For precise individual instances:

1. Select the host actor in the level
2. Move the actor's **pivot** to the desired world position and rotation
3. Click **Add Instance at Pivot** in Details panel for the relevant entry

Fully undoable. Instance count in the Details panel updates immediately.

---

## Step 6 — Validate

Select the host actor → click **Validate Setup** in Details panel.

Fixes all errors reported in the Message Log before running PIE.

Common errors:
- `ProxyClass is null` → assign the Blueprint to the entry
- `NumCustomDataFloats < 2` → resave the host actor; this is auto-set on creation
- `0 instances` → add instances or run the foliage converter

---

## Step 7 — Connect Game Systems (Optional)

By default all instances are eligible for proxy activation. To filter from your game system:

```cpp
// YourSubsystem.cpp — BeginPlay or Initialize
void UHarvestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // Defer binding until world is ready — use a timer or override BeginPlay on a Manager actor
}

void UHarvestSubsystem::BindToBridges(UWorld* World)
{
    for (TActorIterator<AHISMProxyHostActor> It(World); It; ++It)
    {
        for (FHISMProxyInstanceType& Entry : It->InstanceTypes)
        {
            if (!Entry.Bridge) { continue; }
            Entry.Bridge->OnQueryInstanceEligibility.BindUObject(
                this, &UHarvestSubsystem::IsInstanceEligible);
        }
    }
}

bool UHarvestSubsystem::IsInstanceEligible(
    const UHierarchicalInstancedStaticMeshComponent* HISM, int32 InstanceIndex)
{
    // Return false to suppress proxy activation (e.g. harvested tree on cooldown)
    return !HarvestedInstances.Contains(FHISMInstanceKey(HISM, InstanceIndex));
}
```

---

## Step 8 — Notifying State Changes

When an instance state changes and the active proxy should be deactivated immediately (e.g. a tree was harvested):

```cpp
void UHarvestSubsystem::OnTreeHarvested(
    UHISMProxyBridgeComponent* Bridge, int32 InstanceIndex)
{
    // Record harvested state so IsInstanceEligible returns false
    HarvestedInstances.Add(FHISMInstanceKey(Bridge->TargetHISM, InstanceIndex));

    // Deactivate proxy immediately — proxy hides, HISM instance restores
    // (instance won't reactivate because IsInstanceEligible now returns false)
    Bridge->NotifyInstanceStateChanged(InstanceIndex);

    // Respawn after delay — remove from HarvestedInstances so eligibility is restored
    FTimerHandle Handle;
    GetWorld()->GetTimerManager().SetTimer(Handle, [this, Bridge, InstanceIndex]()
    {
        HarvestedInstances.Remove(FHISMInstanceKey(Bridge->TargetHISM, InstanceIndex));
        // Next proximity tick will reactivate if a player is nearby
    }, RespawnDelaySeconds, false);
}
```

---

## Step 9 — Integration with Interaction System

Proxy actors are standard Actors. Add `UInteractionComponent` to the Blueprint exactly as any other interactable:

```cpp
// Inside BP_OnProxyActivated:
// Query your game system for this instance's current state
ETreeState State = HarvestSubsystem->GetTreeState(BoundInstanceIndex);

if (State == ETreeState::Available)
    InteractionComp->SetEntryServerEnabled(0, true);
else
    InteractionComp->SetEntryServerEnabled(0, false);

// Bind the interaction callback
InteractionComp->OnInteractionExecuted.AddDynamic(this, &ABP_OakTreeProxy::OnHarvestInteracted);
```

```cpp
// Inside BP_OnProxyDeactivated:
InteractionComp->OnInteractionExecuted.Clear();
InteractionComp->SetEntryServerEnabled(0, false);
```

The Interaction System's scanner discovers the proxy via standard Actor overlap — no changes required.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| HISM and proxy both visible | Material missing `MF_HISMProxyHide` | Add the material function to every HISM material |
| No proxies activating | No server authority | Run PIE as Listen Server or Dedicated Server |
| `pool exhausted` warnings | `MinPoolSize` too small | Raise using the sizing formula |
| Proxies stuck after PIE | `EndPlay` not deactivating active slots | Verify `EndPlay` iterates `Slots[]` and calls `DeactivateSlotImmediate` |
| Wrong proxy type | Stale type indices after entry reorder | Click Validate Setup; resave the host actor |
| Foliage converter skips a mesh | Mesh not in `InstanceTypes` | Add the entry before running the converter |
| Double rendering after convert | `bRemoveFoliageAfterConversion = false` | Set to `true` and re-run |
| Editor crash on reload | Details customization not unregistered | Verify `ShutdownModule` calls `UnregisterCustomClassLayout` |
