# InventoryTypes & IItemDefinitionProvider

**Sub-page of:** [Inventory System](../Inventory%20System.md)
**File:** `Inventory/InventoryTypes.h`, `Inventory/IItemDefinitionProvider.h`

---

## EInventoryMutationResult

Returned by all `TryPlace` and authority mutation calls. Callers must handle non-`Success` results explicitly.

```cpp
UENUM(BlueprintType)
enum class EInventoryMutationResult : uint8
{
    // Full quantity placed successfully.
    Success,

    // Some quantity placed; remainder could not fit.
    // Only returned by TryPlaceAuto / PlaceAuto.
    PartialSuccess,

    // Current weight + item weight exceeds MaxWeight (soft cap).
    // Server may override; blocks client-predicted placement.
    WeightExceeded,

    // Current weight + item weight exceeds WeightLimit (hard cap).
    // Never overridable. Placement is blocked unconditionally.
    WeightLimitExceeded,

    // No free slot available within MaxSlots.
    SlotsExceeded,

    // Adding this item would exceed SlotLimit (hard cap).
    SlotLimitExceeded,

    // The target slot index does not exist in the layout.
    InvalidSlot,

    // The target tagged slot's URequirementList evaluated to Fail.
    RequirementFailed,

    // IItemDefinitionProvider rejected the item tag as unknown.
    InvalidItem,

    // Item quantity exceeds max stack size and no overflow slot is available.
    StackLimitExceeded,

    // No quantity could be placed (zero-quantity TryPlace input or empty inventory).
    NothingToPlace,
};
```

---

## FInventorySlot

The atomic unit of inventory state. Stored in `FInventorySlotArray` (FastArray).

```cpp
USTRUCT()
struct GAMECORE_API FInventorySlot : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Stable integer identity for this slot. Assigned at creation, never reused.
    // Used as the replication delta key and as the future matrix anchor
    // (matrix will add FIntPoint Position without removing SlotIndex).
    UPROPERTY()
    int32 SlotIndex = INDEX_NONE;

    // What item occupies this slot. FGameplayTag::EmptyTag = slot is empty.
    UPROPERTY()
    FGameplayTag ItemTag;

    // Stack quantity. Always >= 1 when ItemTag is valid.
    UPROPERTY()
    int32 Quantity = 0;

    // Opaque per-instance data blob. GameCore never reads or writes this.
    // The game module owns the schema (durability, enchants, owner tag, etc.).
    // Serialized as raw bytes into the persistence archive.
    UPROPERTY()
    TArray<uint8> InstanceData;

    bool IsEmpty() const { return !ItemTag.IsValid() || Quantity <= 0; }
};
```

> **Matrix extension note:** When matrix layout is added, a `FIntPoint Position` and `FIntPoint Size` will be appended to `FInventorySlot`. `SlotIndex` remains the replication key. Existing saves are migrated via `IPersistableComponent::Migrate()`.

---

## FInventorySlotArray

FastArray wrapper. Only dirty slots are sent in each replication update.

```cpp
USTRUCT()
struct GAMECORE_API FInventorySlotArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FInventorySlot> Items;

    // Required by FFastArraySerializer.
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FInventorySlot,
            FInventorySlotArray>(Items, DeltaParms, *this);
    }

    // Called by FFastArraySerializer after each replicated update on the client.
    // Fires the owning component's OnSlotChanged delegate.
    void PostReplicatedChange(const TArrayView<int32>& ChangedIndices,
                              int32 FinalSize);

    // Helper used by UInventoryComponent.
    FInventorySlot* FindByIndex(int32 SlotIndex);
    const FInventorySlot* FindByIndex(int32 SlotIndex) const;

    int32 NextSlotIndex = 0; // Monotonically increasing; never reused.
};

template<>
struct TStructOpsTypeTraits<FInventorySlotArray>
    : public TStructOpsTypeTraitsBase2<FInventorySlotArray>
{
    enum { WithNetDeltaSerializer = true };
};
```

---

## FInventoryAutoPlaceResult

Returned by `TryPlaceAuto` and `PlaceAuto`. Carries both the result code and quantity accounting.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FInventoryAutoPlaceResult
{
    GENERATED_BODY()

    // Success or PartialSuccess if any quantity was placed.
    // First blocking result code if nothing was placed.
    UPROPERTY(BlueprintReadOnly)
    EInventoryMutationResult Result = EInventoryMutationResult::NothingToPlace;

    // How many units were actually placed.
    UPROPERTY(BlueprintReadOnly)
    int32 QuantityPlaced = 0;

    // How many units could not fit. 0 on full Success.
    UPROPERTY(BlueprintReadOnly)
    int32 QuantityRemaining = 0;
};
```

---

## IItemDefinitionProvider

Optional interface. `UInventoryComponent` holds a weak pointer to an implementor set at `BeginPlay` by the game module. If null, GameCore uses safe defaults (weight = 0, stack size = 1).

```cpp
UINTERFACE(MinimalAPI, Blueprintable)
class GAMECORE_API UItemDefinitionProvider : public UInterface
{
    GENERATED_BODY()
};

class GAMECORE_API IItemDefinitionProvider
{
    GENERATED_BODY()
public:
    // Returns the weight of one unit of this item in game-defined weight units.
    // Called when computing current inventory weight and during TryPlace.
    // Return 0.f for weightless items. Never return negative.
    virtual float GetItemWeight(FGameplayTag ItemTag) const = 0;

    // Returns the maximum units that may occupy a single slot for this item.
    // Return 1 for non-stackable items.
    virtual int32 GetMaxStackSize(FGameplayTag ItemTag) const = 0;

    // Returns false if the tag is not a recognised item. Used in dev builds
    // to catch authoring errors in TryPlace calls.
    virtual bool IsValidItem(FGameplayTag ItemTag) const = 0;
};
```

> **Null safety:** `UInventoryComponent` always null-checks the provider before calling it. A missing provider is not an error in shipping builds — it silently degrades (zero weight, stack size 1). In development builds, `ensure(Provider)` fires once at `BeginPlay` if a weight constraint is active and no provider is set.
