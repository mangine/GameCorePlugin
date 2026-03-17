# UHISMProxyConfig

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`UHISMProxyConfig` is a `UDataAsset` that holds all designer-facing configuration for one HISM proxy setup. One asset per HISM Actor type (e.g. `DA_OakTree_ProxyConfig`, `DA_IronOre_ProxyConfig`). It is referenced by `UHISMProxyBridgeComponent` and read once at `BeginPlay`.

**Files:** `HISMProxy/HISMProxyConfig.h` (header only — no .cpp needed)

---

## Class Definition

```cpp
// Maps a gameplay tag to a concrete proxy actor Blueprint class.
// The tag is returned by FHISMInstanceTypeDelegate to tell the bridge
// which pool to pull from for a given instance.
USTRUCT(BlueprintType)
struct FHISMProxyClassEntry
{
    GENERATED_BODY()

    // Identifies the proxy type. Returned by OnQueryInstanceType delegate.
    // Convention: HISMProxy.<Domain>.<Type>  e.g. HISMProxy.Resource.OakTree
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag ProxyTypeTag;

    // The Blueprint subclass of AHISMProxyActor to spawn for this type.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AHISMProxyActor> ProxyClass;

    // How many pool slots to pre-allocate for this type.
    // Total pool size across all entries should equal UHISMProxyConfig::PoolSize.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
    int32 PoolAllocation = 8;
};


UCLASS(BlueprintType)
class GAMECORE_API UHISMProxyConfig : public UDataAsset
{
    GENERATED_BODY()
public:

    // ── Proximity ─────────────────────────────────────────────────────────────

    // Distance (cm) at which a HISM instance acquires a proxy.
    // Measured from each player pawn's location to the instance world position.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "100.0", ForceUnits = "cm"))
    float ActivationRadius = 1500.f;

    // Extra distance (cm) beyond ActivationRadius before a proxy is considered
    // out of range. Creates hysteresis — proxies do not thrash when a player
    // hovers near the activation boundary.
    // Effective deactivation distance = ActivationRadius + DeactivationRadiusBonus.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "0.0", ForceUnits = "cm"))
    float DeactivationRadiusBonus = 400.f;

    // Seconds to wait after all players have left the effective range before
    // the proxy is deactivated and returned to the pool.
    // If a new player enters range during this window, the timer is cancelled.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "0.0", ForceUnits = "s"))
    float DeactivationDelay = 5.f;

    // How often (seconds) the server runs the proximity check across all players.
    // Lower values = more responsive activation, higher CPU cost.
    // 0.5s is the recommended default for MMO densities.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity",
              meta = (ClampMin = "0.1", ForceUnits = "s"))
    float ProximityTickInterval = 0.5f;

    // Collision channel used for the player proximity sphere overlap.
    // Recommended: create a dedicated ECC_ProxyProximity channel that only
    // player pawns respond to, keeping the overlap result set minimal.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proximity")
    TEnumAsByte<ECollisionChannel> PlayerCollisionChannel = ECC_Pawn;

    // ── Pool ──────────────────────────────────────────────────────────────────

    // Total maximum concurrent live proxies for this bridge.
    // Must equal the sum of PoolAllocation across all ProxyClasses entries.
    // Validated at BeginPlay — mismatch logs a warning and clamps.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool",
              meta = (ClampMin = "1"))
    int32 PoolSize = 32;

    // ── Proxy Classes ─────────────────────────────────────────────────────────

    // Tag → class → pool allocation mapping.
    // Add one entry per distinct proxy actor type this bridge may spawn.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proxy Classes")
    TArray<FHISMProxyClassEntry> ProxyClasses;

    // ── Spatial Grid ──────────────────────────────────────────────────────────

    // Side length (cm) of each grid cell used by FHISMInstanceSpatialGrid.
    // Should be roughly equal to ActivationRadius for optimal lookup.
    // Smaller = fewer false positives per query, more memory.
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

## Validation (`IsDataValid`)

```cpp
// Pseudo-code for editor validation
EDataValidationResult UHISMProxyConfig::IsDataValid(FDataValidationContext& Context) const
{
    int32 TotalAllocated = 0;
    for (const FHISMProxyClassEntry& Entry : ProxyClasses)
    {
        if (!Entry.ProxyTypeTag.IsValid())
            Context.AddError(TEXT("ProxyClasses entry has an invalid tag."));
        if (Entry.ProxyClass == nullptr)
            Context.AddError(TEXT("ProxyClasses entry has a null ProxyClass."));
        TotalAllocated += Entry.PoolAllocation;
    }
    if (TotalAllocated != PoolSize)
        Context.AddWarning(FText::Format(
            INVTEXT("PoolSize ({0}) does not match sum of PoolAllocation ({1})."),
            PoolSize, TotalAllocated));

    return Context.GetNumErrors() > 0
        ? EDataValidationResult::Invalid
        : EDataValidationResult::Valid;
}
```

---

## Tag Convention

Proxy type tags live under `HISMProxy.<Domain>.<Type>`:

```
HISMProxy.Resource.OakTree
HISMProxy.Resource.IronOre
HISMProxy.Resource.FishingSpot
HISMProxy.Prop.Barrel
HISMProxy.Prop.Crate
```

Declare these in the project's `GameplayTags.ini` or a dedicated `HISMProxy.ini` tag source.

---

## Notes

- `ProximityTickInterval` is the primary CPU budget knob. At 0.5s with 64 players and 500 instances per bridge, the proximity check runs 128 per second — keep `GridCellSize` well-tuned to ensure each check touches only a small candidate set.
- `PlayerCollisionChannel`: creating a dedicated `ECC_ProxyProximity` channel in `DefaultEngine.ini` and setting only `APlayerState`-owned pawns to respond to it eliminates all non-player overlap noise from the proximity sphere query at zero gameplay cost.
- `PoolAllocation` per type entry must be sized to worst-case concurrent activations for that type, not average. Under-allocation causes silent skips with a logged warning.
