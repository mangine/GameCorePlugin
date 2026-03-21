# UHISMProxyConfig

**File:** `GameCore/Source/GameCore/Public/HISMProxy/HISMProxyConfig.h` (header-only, no `.cpp` needed)  
**Module:** `GameCore`

A `UDataAsset` holding proximity and timing configuration for one HISM proxy setup. Referenced by each `FHISMProxyInstanceType` entry on `AHISMProxyHostActor`. Multiple instance types can share the same config asset when their proximity behaviour is identical.

Contains **only** proximity/timing data. Mesh, proxy class, and pool sizes live on `FHISMProxyInstanceType` so they are visible directly on the host actor without opening a separate asset.

---

## Class Definition

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UHISMProxyConfig : public UDataAsset
{
    GENERATED_BODY()
public:

    // ── Proximity ─────────────────────────────────────────────────────────

    // Distance (cm) at which a HISM instance acquires a proxy.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "100.0", ForceUnits = "cm"))
    float ActivationRadius = 1500.f;

    // Extra distance (cm) beyond ActivationRadius before a proxy is considered
    // out of range. Prevents boundary thrashing.
    // Effective deactivation radius = ActivationRadius + DeactivationRadiusBonus.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "0.0", ForceUnits = "cm"))
    float DeactivationRadiusBonus = 400.f;

    // Seconds after all players leave before the proxy deactivates.
    // Cancelled if a player re-enters during this window.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "0.0", ForceUnits = "s"))
    float DeactivationDelay = 5.f;

    // Server proximity check interval (seconds).
    // 0.5s recommended for MMO-scale player counts.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "0.1", ForceUnits = "s"))
    float ProximityTickInterval = 0.5f;

    // ── Spatial Grid ──────────────────────────────────────────────────────

    // Cell size (cm) for the spatial acceleration grid.
    // Should approximately equal ActivationRadius for optimal query performance.
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

## Validation

```cpp
#if WITH_EDITOR
EDataValidationResult UHISMProxyConfig::IsDataValid(
    FDataValidationContext& Context) const
{
    if (ActivationRadius <= 0.f)
        Context.AddError(TEXT("ActivationRadius must be > 0."));
    if (GridCellSize <= 0.f)
        Context.AddError(TEXT("GridCellSize must be > 0."));
    if (ProximityTickInterval < 0.1f)
        Context.AddWarning(
            TEXT("ProximityTickInterval < 0.1s may cause high CPU load on servers."));

    return Context.GetNumErrors() > 0
        ? EDataValidationResult::Invalid
        : EDataValidationResult::Valid;
}
#endif
```

---

## Notes

- `GridCellSize` should be kept close to `ActivationRadius`. If they diverge significantly (e.g. `ActivationRadius=500`, `GridCellSize=3000`), the grid query touches many cells unnecessarily, increasing false-positive candidate count.
- One config asset is sufficient for most projects. Create separate assets only when proximity behaviour genuinely differs (e.g. indoor interactable vs. distant terrain foliage).
- `DeactivationRadiusBonus` adds hysteresis. A player hovering exactly at the activation boundary will not cause repeated activate/deactivate cycles.
