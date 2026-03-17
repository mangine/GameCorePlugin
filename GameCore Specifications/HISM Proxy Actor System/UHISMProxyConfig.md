# UHISMProxyConfig

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`UHISMProxyConfig` is a `UDataAsset` holding proximity and timing configuration for one HISM proxy setup. It is referenced by each `FHISMProxyInstanceType` entry in `AHISMProxyHostActor`. Multiple instance types can share the same config asset if their proximity behavior is identical.

This asset intentionally contains **only** proximity/timing data — mesh, proxy class, and pool size live on `FHISMProxyInstanceType` so they are visible directly on the host actor without opening a separate asset.

**Files:** `HISMProxy/HISMProxyConfig.h` (header only — no .cpp needed)

---

## Class Definition

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UHISMProxyConfig : public UDataAsset
{
    GENERATED_BODY()
public:

    // ── Proximity ─────────────────────────────────────────────────────────────

    // Distance (cm) from a player pawn at which a HISM instance acquires a proxy.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "100.0", ForceUnits = "cm"))
    float ActivationRadius = 1500.f;

    // Extra distance (cm) added to ActivationRadius before a proxy is considered
    // out of range. Prevents thrashing when a player hovers at the boundary.
    // Effective deactivation radius = ActivationRadius + DeactivationRadiusBonus.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "0.0", ForceUnits = "cm"))
    float DeactivationRadiusBonus = 400.f;

    // Seconds to wait after all players have left the effective range before
    // the proxy is deactivated. Cancelled if a player re-enters during this window.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "0.0", ForceUnits = "s"))
    float DeactivationDelay = 5.f;

    // How often (seconds) the server runs the proximity check.
    // 0.5s is recommended for MMO-scale player counts.
    // Lower = more responsive, higher CPU cost.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "0.1", ForceUnits = "s"))
    float ProximityTickInterval = 0.5f;

    // ── Spatial Grid ──────────────────────────────────────────────────────────

    // Cell size (cm) for the spatial acceleration grid.
    // Should approximately equal ActivationRadius for optimal query performance.
    // Smaller = fewer false positives, more memory. Larger = more false positives, less memory.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spatial Grid",
              meta = (ClampMin = "100.0", ForceUnits = "cm"))
    float GridCellSize = 1500.f;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
#endif
};
```

---

## What Moved Out of This Asset

The previous version of `UHISMProxyConfig` contained `PoolSize`, `ProxyClasses` (array of tag→class mappings), and `PoolAllocation` per entry. These have been removed:

| Field | Moved To | Reason |
|---|---|---|
| `PoolSize` | `FHISMProxyInstanceType::PoolSize` | Visible directly on the host actor; no separate asset needed |
| `ProxyClasses` | `FHISMProxyInstanceType::ProxyClass` | One class per bridge; tag routing was unnecessary complexity |
| `PoolAllocation` | Removed | Each bridge has one class; allocation = PoolSize |
| `ProxyTypeTag` | Removed | Bridges no longer need type tags; each manages a homogeneous HISM |

This simplification is correct because the one-HISM-per-mesh-type architecture means each bridge already knows its proxy class without needing a tag dispatch.

---

## Sharing Config Assets

Config assets are designed to be shared. A forest, shipyard, and fishing village might all use the same `DA_Default_ProxyConfig` with `ActivationRadius=1500`, `DeactivationDelay=5`. Only assets with meaningfully different proximity requirements (e.g. interactive barrels close to a shop need a shorter radius than distant trees) need separate assets.

---

## Validation

```cpp
EDataValidationResult UHISMProxyConfig::IsDataValid(FDataValidationContext& Context) const
{
    if (ActivationRadius <= 0.f)
        Context.AddError(TEXT("ActivationRadius must be > 0."));
    if (GridCellSize <= 0.f)
        Context.AddError(TEXT("GridCellSize must be > 0."));
    if (ProximityTickInterval < 0.1f)
        Context.AddWarning(TEXT("ProximityTickInterval < 0.1s may cause high CPU load on servers."));

    return Context.GetNumErrors() > 0
        ? EDataValidationResult::Invalid
        : EDataValidationResult::Valid;
}
```

---

## Notes

- A single config asset is sufficient for most projects. Create additional assets only when proximity behavior genuinely differs between context types.
- `GridCellSize` should be kept close to `ActivationRadius`. If they diverge significantly (e.g. `ActivationRadius=500`, `GridCellSize=3000`), the query touches many cells unnecessarily.
