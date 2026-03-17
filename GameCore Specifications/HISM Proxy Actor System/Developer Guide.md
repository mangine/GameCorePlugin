# Developer Guide — HISM Proxy Actor System

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

This guide is the practical reference for setting up and using the HISM Proxy Actor System. It covers both **mass placement via the Foliage Tool** (recommended for forests, prop clusters) and **manual placement** (recommended for precise single-instance positioning).

---

## Prerequisites

1. `GameCore` plugin is enabled in your project.
2. `GameCoreEditor` module is listed in your project's `.uproject` as an editor module.
3. Your project has at least one HISM-compatible material (see Material Setup below).

---

## Step 1 — Prepare the HISM Material

Every static mesh used in the HISM Proxy system must have a material that reads `PerInstanceCustomData[0]` and discards the pixel when the hide flag is set.

This is a **one-time setup per material**. Do it once, share the material across all HISM mesh types.

```
Material Graph:

  [PerInstanceCustomData]  index=0, default=0.0
         |
         v
  [If]  A >= 0.5 → return -1 (discard)
         |        → return 1  (render normally)
         |
  [Clip] input from [If]
```

In practice: add a **PerInstanceCustomData** node (index 0), feed it into a **Clip** node. Any value ≥ 0.5 discards the pixel. The rest of the material graph connects normally to Base Color, Roughness, etc.

> **Important:** HISM materials must have `bUsedWithInstancedStaticMeshes = true`. This is set automatically when assigning a material to a HISM component, but verify it in the Material settings.

---

## Step 2 — Create a Proxy Config Asset

In the Content Browser: right-click → **GameCore → HISM Proxy Config**.

Name it descriptively: `DA_Forest_ProxyConfig`, `DA_Shipyard_ProxyConfig`, etc.

Configure:

| Property | Typical Value | Notes |
|---|---|---|
| `ActivationRadius` | 1500 cm | When a player gets within 15m, proxies activate |
| `DeactivationRadiusBonus` | 400 cm | 4m hysteresis band |
| `DeactivationDelay` | 5.0 s | Proxy stays live 5s after player leaves |
| `ProximityTickInterval` | 0.5 s | Check twice per second |
| `GridCellSize` | 1500 cm | Match ActivationRadius |

A single config asset works for most scenarios. Create additional assets only when proximity behavior genuinely differs (e.g. a tightly packed ship interior where 400cm radius makes more sense).

---

## Step 3 — Create Proxy Actor Blueprints

For each mesh type that needs gameplay behavior (interaction, harvesting, etc.), create a Blueprint:

1. Create a new Blueprint with parent class **`AHISMProxyActor`**
2. Add components as needed: `UInteractionComponent`, `UStaticMeshComponent` (only if the proxy needs a different mesh than the HISM), etc.
3. Override **`BP_OnProxyActivated`**: initialize state from your game system using `BoundInstanceIndex`
4. Override **`BP_OnProxyDeactivated`**: clean up delegates and transient state

**Example — `BP_OakTreeProxy`:**

```
BP_OnProxyActivated(InstanceIndex)
  → Get HarvestSubsystem
  → QueryInstanceState(InstanceIndex)
    → if Harvested: InteractionComp.SetEntryServerEnabled(0, false)
    → if Available: InteractionComp.SetEntryServerEnabled(0, true)
  → Bind InteractionComp.OnInteractionExecuted → OnHarvestInteracted

BP_OnProxyDeactivated
  → Unbind all delegates on InteractionComp
  → Clear any active timers (cooldown countdown, etc.)

OnHarvestInteracted(Instigator, EntryIndex)
  → HarvestSubsystem.HarvestTree(BoundInstanceIndex, Instigator)
  → InteractionComp.SetEntryServerEnabled(0, false)
```

---

## Step 4 — Place an `AHISMProxyHostActor`

1. In the level, **Place Actor** → search for `HISMProxyHostActor` (or a Blueprint subclass)
2. In the **Details panel**, expand **HISM Proxy → Instance Types**
3. Click **+** to add an entry for each mesh type:

| Field | Value |
|---|---|
| `TypeName` | `Oak` |
| `Mesh` | `SM_OakTree` |
| `ProxyClass` | `BP_OakTreeProxy` |
| `Config` | `DA_Forest_ProxyConfig` |
| `PoolSize` | `12` |

After filling in the entry, the system **automatically**:
- Creates `HISM_Oak` component with `NumCustomDataFloats=2`
- Creates `Bridge_Oak` component wired to `HISM_Oak`
- No collision on HISM — proxies handle gameplay collision

Repeat for each tree type (Pine, Birch, etc.).

---

## Step 5A — Mass Placement via Foliage Tool (Recommended)

This is the fastest workflow for placing hundreds of instances.

**Paint with the Foliage Tool normally:**
1. Open the Foliage Tool (Shift+4)
2. Add your meshes (`SM_OakTree`, `SM_PineTree`, `SM_BirchTree`)
3. Paint across the terrain as usual — density, slope filters, random scale, all work
4. Do **not** worry about interactivity at this stage — the Foliage Tool is for bulk placement only

**Convert to the Proxy Host:**
1. In the Content Browser, create an **Editor Utility Object** with parent `UHISMFoliageConversionUtility`
2. Open it, set `TargetHostActor` to your host actor in the level
3. Set `bRemoveFoliageAfterConversion = true`
4. Click **Convert Foliage To Proxy Host** in the Details panel
5. The utility reads all foliage instances, matches meshes to your `InstanceTypes` entries, copies transforms, and removes them from the Foliage Actor

> **Scale variation:** The Foliage Tool supports random scale per instance. These scales are preserved in the instance transform during conversion. No additional setup needed for size variety.

**What gets carried over:**
- World position ✓
- Rotation ✓
- Scale (including random scale) ✓
- `PerInstanceCustomData[0]` = 0.0 (visible) ✓
- `PerInstanceCustomData[1]` = type index ✓

**What does NOT carry over:**
- Foliage Tool collision settings (proxy handles collision)
- Foliage LOD bias (configure on the HISM component directly)

---

## Step 5B — Manual Instance Placement

For precise placement of individual instances (a specific barrel next to a door, a fishing spot at an exact shoreline position):

1. Select the `AHISMProxyHostActor` in the level
2. In the Details panel, under **HISM Proxy**, find the entry you want (e.g. `Oak — (47 instances)`)
3. Move the host actor's **pivot** to the desired world position and rotation
4. Click **Add Instance at Pivot** for that entry
5. The instance is added at the current pivot transform
6. Repeat as needed

> **Tip:** You can temporarily move the host actor to rough-position instances, then use the **Add Instance at Pivot** button. The host actor's transform is used only for placement — the instance world transform is stored and does not change when you move the host actor afterward.

---

## Step 6 — Validate

With the host actor selected, click **Validate Setup** in the Details panel. This checks:

- All entries have a Mesh, ProxyClass, and Config
- All HISM components have `NumCustomDataFloats >= 2`
- No entry has 0 instances (warning, not error)
- No entry has `PoolSize = 0`
- Bridge components are wired correctly

Fix any errors shown in the Message Log before testing in PIE.

---

## Step 7 — Connect Game Systems (Optional)

By default, **all instances are eligible** for proxy activation when a player is nearby. If your game system needs to filter instances (e.g. harvested trees should not get a proxy until respawned), bind `OnQueryInstanceEligibility` on each bridge:

```cpp
// In your game subsystem or manager's BeginPlay:
void UHarvestSubsystem::OnBeginPlay()
{
    // Find all host actors in the world.
    for (TActorIterator<AHISMProxyHostActor> It(GetWorld()); It; ++It)
    {
        for (FHISMProxyInstanceType& Entry : It->InstanceTypes)
        {
            if (!Entry.Bridge) { continue; }

            Entry.Bridge->OnQueryInstanceEligibility.BindUObject(
                this, &UHarvestSubsystem::IsTreeInstanceEligible);
        }
    }
}

bool UHarvestSubsystem::IsTreeInstanceEligible(
    const UHierarchicalInstancedStaticMeshComponent* HISM, int32 InstanceIndex)
{
    // Return false for instances on respawn cooldown.
    return !HarvestedInstances.Contains(FHISMInstanceKey(HISM, InstanceIndex));
}
```

When a harvest completes and the instance should go on cooldown:

```cpp
void UHarvestSubsystem::OnTreeHarvested(
    UHISMProxyBridgeComponent* Bridge, int32 InstanceIndex)
{
    HarvestedInstances.Add(FHISMInstanceKey(Bridge->TargetHISM, InstanceIndex));

    // Force immediate proxy deactivation. The HISM instance reappears.
    Bridge->NotifyInstanceStateChanged(InstanceIndex);

    // Schedule respawn — when the timer fires, remove from HarvestedInstances.
    // The next proximity tick will activate a new proxy if a player is still nearby.
    FTimerHandle RespawnTimer;
    GetWorld()->GetTimerManager().SetTimer(
        RespawnTimer,
        FTimerDelegate::CreateUObject(this,
            &UHarvestSubsystem::OnTreeRespawned,
            Bridge, InstanceIndex),
        RespawnCooldownSeconds, false);
}
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| HISM instance and proxy both visible | Material not reading `PerInstanceCustomData[0]` | Add Clip node reading slot 0 to the material |
| No proxies activating in PIE | Bridge has no authority (listen server: check `HasAuthority`) | PIE must run as server or listen server |
| Pool exhausted warnings in log | `PoolSize` too small | Increase `PoolSize` on the affected `InstanceType` entry |
| Instances disappeared after level reload | `EndPlay` not restoring visibility | Verify `EndPlay` calls `DeactivateSlotImmediate` for all active slots |
| Foliage converter skips a mesh | Mesh not in `InstanceTypes` | Add the mesh as an entry before running the converter |
| Wrong proxy activating for an instance | Type indices rebuilt after entry reorder | Validate Setup after any reorder; check log for type index warnings |
| Proxy spawns at world origin briefly | Normal — pool actors spawn at origin, then teleport on activation | Expected behavior; proxies are hidden at origin |

---

## Summary — Full Developer Flow

```
1. Material Setup (once per mesh)
   └── Add PerInstanceCustomData[0] clip to HISM materials

2. Create DA_*_ProxyConfig (once per proximity profile)
   └── Set radii, delay, tick interval

3. Create BP_*Proxy Blueprints (once per gameplay-active mesh type)
   └── AHISMProxyActor subclass
   └── Add InteractionComponent, bind harvest/interact delegates

4. Place AHISMProxyHostActor
   └── Add InstanceTypes entries (Mesh + ProxyClass + Config + PoolSize)
   └── Components auto-created

5A. Paint with Foliage Tool → Run UHISMFoliageConversionUtility
    └── Instances imported with transforms + type indices

5B. OR: Manual placement via "Add Instance at Pivot" button
    └── Move host actor pivot to desired position → click Add

6. Validate Setup
   └── Fix any errors in Message Log

7. (Optional) Bind OnQueryInstanceEligibility per bridge
   └── Filter ineligible instances from your game system's BeginPlay

8. Test in PIE (as Listen Server or Dedicated Server)
   └── Walk near instances → proxies activate
   └── Walk away → proxies deactivate after delay
```
