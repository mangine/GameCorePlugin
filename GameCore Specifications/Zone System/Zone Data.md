# Zone Data

Defines all data types used by the Zone System. No gameplay logic lives here.

---

## `EZoneShapeType`

```cpp
UENUM(BlueprintType)
enum class EZoneShapeType : uint8
{
    Box,
    ConvexPolygon
};
```

---

## `FZoneShapeData`

Serializable shape descriptor used for runtime zone spawning.

```cpp
USTRUCT(BlueprintType)
struct FZoneShapeData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    EZoneShapeType ShapeType = EZoneShapeType::Box;

    // Box only: half-extents in local space
    UPROPERTY(EditAnywhere)
    FVector BoxExtent = FVector(500.f);

    // ConvexPolygon only: XY points in local space (min 3, wound CCW from above)
    UPROPERTY(EditAnywhere)
    TArray<FVector2D> PolygonPoints;

    // ConvexPolygon only: world-space Z range
    UPROPERTY(EditAnywhere)
    float MinZ = -500.f;

    UPROPERTY(EditAnywhere)
    float MaxZ = 500.f;
};
```

---

## `UZoneDataAsset`

Static, authored-in-editor data. Never mutated at runtime.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UZoneDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // Human-readable identity tag, e.g. Zone.BlackpearlBay
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FGameplayTag ZoneNameTag;

    // Type taxonomy defined by the game module, e.g. Zone.Type.Territory
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FGameplayTag ZoneTypeTag;

    // Arbitrary static tags available to systems that query this zone
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FGameplayTagContainer StaticGameplayTags;

    // Friendly display name (localised)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FText DisplayName;

    // Optional priority for overlap resolution (higher = evaluated first)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    int32 Priority = 0;
};
```

---

## `FZoneDynamicState`

Mutable zone state replicated from server to all clients. Kept minimal — only data that actually changes at runtime.

```cpp
USTRUCT(BlueprintType)
struct FZoneDynamicState
{
    GENERATED_BODY()

    // e.g. the faction/player that currently owns this zone
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag OwnerTag;

    // Runtime-added tags (buffs, event states, etc.)
    UPROPERTY(BlueprintReadOnly)
    FGameplayTagContainer DynamicTags;

    bool operator==(const FZoneDynamicState& Other) const
    {
        return OwnerTag == Other.OwnerTag && DynamicTags == Other.DynamicTags;
    }
};
```

> **Note:** `FZoneDynamicState` uses UE's standard struct delta serialisation. Mark the containing actor property with `ReplicatedUsing=OnRep_DynamicState` so clients can react to changes even when not transitioning zones.
