# UZoneDataAsset

**File:** `GameCore/Source/GameCore/Zone/ZoneDataAsset.h` / `.cpp`

Static, authored-in-editor zone data. Never mutated at runtime. Game modules subclass this to add domain-specific fields.

---

## Class Definition

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UZoneDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // Unique identity tag, e.g. Zone.BlackpearlBay
    // Used by UZoneSubsystem::GetZoneByName()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FGameplayTag ZoneNameTag;

    // Type taxonomy defined by the game module, e.g. Zone.Type.Territory
    // Used for GetZonesByType() and type-filtered event listeners
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FGameplayTag ZoneTypeTag;

    // Arbitrary static tags available to systems querying this zone
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FGameplayTagContainer StaticGameplayTags;

    // Localised display name for UI
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    FText DisplayName;

    // Priority for overlap resolution. Higher value = evaluated first in QueryZonesAtPoint results.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    int32 Priority = 0;
};
```

---

## Notes

- Subclass in the game module to add domain-specific fields (tax rates, ambient audio, danger level, etc.). The Zone System never inspects these fields.
- `ZoneNameTag` is expected to be unique per zone. `GetZoneByName()` returns the first match — duplicate names produce undefined lookup behaviour.
- `Priority` is only used for result ordering in `QueryZonesAtPoint`. It does not affect containment logic.
