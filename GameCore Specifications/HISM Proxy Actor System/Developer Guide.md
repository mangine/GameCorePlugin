# Developer Guide — HISM Proxy Actor System

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

This guide is the practical reference for setting up and using the HISM Proxy Actor System end-to-end.

---

## Prerequisites

- `GameCore` plugin enabled in your project.
- `GameCoreEditor` listed as an editor-only module in your `.uproject`.
- At least one HISM-compatible material prepared (see Step 1).

---

## Step 1 — Prepare the HISM Material

Every static mesh used in the system must have a material that discards pixels when the hide flag is set in `PerInstanceCustomData[0]`.

**Recommended:** Create a shared Material Function **`MF_HISMProxyHide`** at
`Content/GameCore/Materials/Functions/MF_HISMProxyHide.uasset`.

```
MF_HISMProxyHide implementation:
  [PerInstanceCustomData]  Index=0, Default=0.0
        |
  [If]  A >= 0.5  →  -1.0 (discard)
                  →   1.0 (render)
        |
  [Clip]
```

Call this function in every HISM material's graph — no parameters, no per-material setup. All artists need to do is drop in the function node.

> **Required material flag:** `bUsedWithInstancedStaticMeshes = true`. This is set automatically when assigning a material to a HISM component, but verify it in Material Settings if rendering issues occur.

**Why a Material Function?** Manually wiring the `PerInstanceCustomData` clip in each material is error-prone — a misplaced node or wrong slot index silently breaks instance hiding. The function encapsulates the logic once and eliminates that risk.

---

## Step 2 — Create a Proxy Config Asset

Right-click in Content Browser → **GameCore → HISM Proxy Config**.

| Property | Typical Value | Notes |
|---|---|---|
| `ActivationRadius` | 1500 cm | Player must be within 15m to activate a proxy |
| `DeactivationRadiusBonus` | 400 cm | 4m hysteresis prevents boundary thrashing |
| `DeactivationDelay` | 5.0 s | Proxy stays live 5s after all players leave |
| `ProximityTickInterval` | 0.5 s | Server checks twice per second |
| `GridCellSize` | 1500 cm | Match `ActivationRadius` for optimal query performance |

One config asset is sufficient for most projects. Create additional ones only when proximity behavior genuinely differs between contexts.

---

## Step 3 — Create Proxy Actor Blueprints

For each mesh type that needs gameplay behavior, create a Blueprint:

1. Parent class: **`AHISMProxyActor`**
2. Add any gameplay components: `UInteractionComponent`, audio, VFX, etc.
3. Override **`BP_OnProxyActivated(InstanceIndex)`**: query your game system using `BoundInstanceIndex`, configure the proxy's runtime state.
4. Override **`BP_OnProxyDeactivated`**: unbind delegates, stop timers, flush any sub-completion state to your game system's persistent storage.

> **Partial state and the DeactivationDelay:** The proxy actor does not retain any state across pool cycles. If a player partially completes an interaction (e.g. 2 of 5 axe swings on a tree) and leaves, that partial state is lost when the proxy deactivates. This is **by design** — the `DeactivationDelay` (default 5s) gives game systems a window to save sub-completion state to external storage before `OnProxyDeactivated` fires. Design your game systems to persist partial state when `OnProxyDeactivated` is called, not when the interaction completes.

---

## Step 4 — Place an `AHISMProxyHostActor`

1. Place Actor → search **HISM Proxy Host Actor**
2. In **Details → HISM Proxy → Instance Types**, click **+** per mesh type:

| Field | Example Value |
|---|---|
| `TypeName` | `Oak` |
| `Mesh` | `SM_OakTree` |
| `ProxyClass` | `BP_OakTreeProxy` |
| `Config` | `DA_Forest_ProxyConfig` |
| `MinPoolSize` | see formula below |
| `MaxPoolSize` | theoretical maximum (safety ceiling) |
| `GrowthBatchSize` | 8 |

After filling each entry, the system automatically creates `HISM_Oak` (with `NumCustomDataFloats=2`) and `Bridge_Oak` (wired to `HISM_Oak`). No manual component setup.

### Pool Sizing Formula

```
InstanceDensity = TotalInstances / AreaM²
RadiusM         = ActivationRadius / 100.0
MaxPerPlayer    = PI * RadiusM² * InstanceDensity
MinPoolSize     = ceil(MaxPerPlayer * ExpectedConcurrentPlayers * 1.2)
```

**Example:** 500 oaks / 40,000m², radius=15m, 64 players:
```
Density      = 500/40000 = 0.0125 /m²
MaxPerPlayer = PI * 225 * 0.0125 ≈ 8.8
MinPoolSize  = ceil(8.8 * 64 * 1.2) = 676
```
In practice players cluster — `MinPoolSize = 300–400` is often sufficient. Set `MaxPoolSize = 676` as the hard ceiling. If `Warning: pool exhausted` appears in logs, raise `MinPoolSize`.

---

## Step 5A — Mass Placement via Foliage Tool (Recommended)

**Paint instances normally:**
1. Open Foliage Tool (Shift+4), add your meshes
2. Paint across terrain — density, slope, random scale all work
3. Scale variation is handled per-instance by the Foliage Tool; no separate HISM component per size needed

**Convert to the Proxy Host:**
1. Right-click Content Browser → **Editor Utilities → Editor Utility Object**, parent `UHISMFoliageConversionUtility`
2. Set `TargetHostActor` to your host actor
3. Set `bRemoveFoliageAfterConversion = true`
4. Click **Convert Foliage To Proxy Host**

The converter runs in three passes (collect transforms → add to host → remove from foliage), is fully undoable via Ctrl+Z, and preserves all per-instance transform data including random scales.

---

## Step 5B — Manual Instance Placement

For precise individual instances (a specific barrel next to a door, a fishing spot at an exact shoreline position):

1. Select the host actor in the level
2. Move the actor's **pivot** to the desired world position and rotation
3. In Details panel, click **Add Instance at Pivot** for the relevant entry
4. Repeat as needed

This is undoable. The instance count in the Details panel updates after each add.

> **Known limitation:** Moving the pivot is the only placement mechanism currently available without a viewport FEdMode tool. For bulk precise placement, prefer the Foliage converter.

---

## Step 6 — Validate

Select the host actor → click **Validate Setup** in Details panel.

Checks:
- All entries have Mesh, ProxyClass, Config
- `NumCustomDataFloats >= 2` on each HISM
- No entry has 0 instances (warning)
- `MinPoolSize >= 1`, `MaxPoolSize >= MinPoolSize`
- Bridge and HISM components present and wired

Fix all errors before PIE testing.

---

## Step 7 — Connect Game Systems (Optional)

By default all instances are eligible. To filter instances from your game system:

```cpp
// In your subsystem's BeginPlay or initialization:
for (TActorIterator<AHISMProxyHostActor> It(GetWorld()); It; ++It)
{
    for (FHISMProxyInstanceType& Entry : It->InstanceTypes)
    {
        if (!Entry.Bridge) { continue; }
        Entry.Bridge->OnQueryInstanceEligibility.BindUObject(
            this, &UHarvestSubsystem::IsInstanceEligible);
    }
}

bool UHarvestSubsystem::IsInstanceEligible(
    const UHierarchicalInstancedStaticMeshComponent* HISM, int32 InstanceIndex)
{
    return !HarvestedSet.Contains(FHISMInstanceKey(HISM, InstanceIndex));
}
```

When an instance changes state:

```cpp
void UHarvestSubsystem::OnTreeHarvested(
    UHISMProxyBridgeComponent* Bridge, int32 InstanceIndex)
{
    HarvestedSet.Add(FHISMInstanceKey(Bridge->TargetHISM, InstanceIndex));
    Bridge->NotifyInstanceStateChanged(InstanceIndex); // deactivates proxy immediately

    // Schedule respawn — when fired, remove from HarvestedSet.
    // Next proximity tick will activate a new proxy if a player is still nearby.
    GetWorld()->GetTimerManager().SetTimer(/* ... RespawnDelay ... */);
}
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| HISM and proxy both visible | Material not reading `PerInstanceCustomData[0]` | Add `MF_HISMProxyHide` to the material |
| No proxies activating | Bridge has no authority | Run PIE as Listen Server or Dedicated Server |
| `pool exhausted` warnings in log | `MinPoolSize` undersized | Raise `MinPoolSize` using the sizing formula |
| Pool growth warnings in log | Same as above | Same fix; `MaxPoolSize` is the safety ceiling |
| Instances disappeared after PIE | `EndPlay` not restoring visibility | Verify `EndPlay` is calling `DeactivateSlotImmediate` for all active slots |
| Wrong proxy type activating | Stale type indices after entry reorder | Run **Validate Setup**; `RebuildTypeIndices` fixes it on next save |
| Foliage converter skips a mesh | Mesh not in `InstanceTypes` | Add the mesh entry before running the converter |
| Double rendering after conversion | `bRemoveFoliageAfterConversion = false` | Set to `true` and re-run the converter |
| Crash on editor shutdown | Details customization not unregistered | Verify `ShutdownModule` calls `UnregisterCustomClassLayout` |

---

## Full Developer Flow Summary

```
1. Material setup (once per mesh)
   └── Create MF_HISMProxyHide, call it in every HISM material

2. Create DA_*_ProxyConfig (once per proximity profile)
   └── Set radii, delay, tick interval

3. Create BP_*Proxy Blueprints (once per gameplay-active mesh type)
   └── AHISMProxyActor subclass
   └── Override BP_OnProxyActivated / BP_OnProxyDeactivated
   └── Flush partial state to game system in BP_OnProxyDeactivated

4. Place AHISMProxyHostActor
   └── Add InstanceTypes entries (TypeName, Mesh, ProxyClass, Config, pool sizes)
   └── HISM + Bridge components auto-created and wired

5A. Paint with Foliage Tool → Run UHISMFoliageConversionUtility
    └── Three-pass safe conversion; fully undoable

5B. OR: Manual placement via "Add Instance at Pivot" button
    └── Undoable

6. Validate Setup → fix all Message Log errors

7. (Optional) Bind OnQueryInstanceEligibility per bridge
   └── From game subsystem BeginPlay

8. Test in PIE (Listen Server or Dedicated Server)
   └── Walk near instances → proxies activate
   └── Walk away → proxies deactivate after DeactivationDelay
   └── Check logs for "pool exhausted" warnings → raise MinPoolSize if seen
```
