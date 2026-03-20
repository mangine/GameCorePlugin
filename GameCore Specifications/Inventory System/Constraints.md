# Constraints

**Sub-page of:** [Inventory System](../Inventory%20System.md)
**Files:** `Inventory/Constraints/InventoryConstraint.h/.cpp`, `WeightConstraint.h/.cpp`, `SlotCountConstraint.h/.cpp`

Constraints are instanced `UObject` subclasses placed inline on `UInventoryComponent`. They are independent — zero, one, or both may be active simultaneously. The server evaluates all constraints during `TryPlace`; any blocking result short-circuits.

---

## UInventoryConstraint — Abstract Base

```cpp
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UInventoryConstraint : public UObject
{
    GENERATED_BODY()
public:
    // Pure validation. Never mutates inventory state.
    // Called by UInventoryComponent::RunConstraints during TryPlace.
    // Must be const — no side effects.
    virtual EInventoryMutationResult TestAdd(
        const UInventoryComponent& Inventory,
        FGameplayTag ItemTag,
        int32 Quantity) const PURE_VIRTUAL(UInventoryConstraint::TestAdd,
            return EInventoryMutationResult::Success;);

    // Called after a successful placement to allow the constraint to update
    // any cached state (e.g. current weight). Default no-op.
    virtual void OnItemAdded(const UInventoryComponent& Inventory,
                             FGameplayTag ItemTag, int32 Quantity) {}

    // Called after any removal. Default no-op.
    virtual void OnItemRemoved(const UInventoryComponent& Inventory,
                               FGameplayTag ItemTag, int32 Quantity) {}
};
```

> **Design rule:** `TestAdd` is a pure predicate. All broadcast and state changes happen in `OnItemAdded`/`OnItemRemoved` or in `UInventoryComponent` after the mutation commits. Never fire delegates inside `TestAdd`.

---

## UWeightConstraint

```cpp
UCLASS(EditInlineNew, DefaultToInstanced,
       meta=(DisplayName="Weight Constraint"))
class GAMECORE_API UWeightConstraint : public UInventoryConstraint
{
    GENERATED_BODY()
public:

    // Hard cap. Designer-set. Cannot be exceeded regardless of buffs.
    // If 0, no hard cap is enforced.
    UPROPERTY(EditDefaultsOnly, Category="Weight", meta=(ClampMin=0))
    float WeightLimit = 0.f;

    // Current effective cap. Set at BeginPlay by the game module via
    // NotifyMaxWeightChanged(). Defaults to WeightLimit if never set.
    // May be exceeded if WeightLimit is 0.
    float GetMaxWeight() const { return MaxWeight; }
    float GetCurrentWeight() const { return CurrentWeight; }

    // Called by the game module whenever the GAS MaxCarryWeight attribute changes.
    // Safe to call from any GAS attribute delegate.
    void NotifyMaxWeightChanged(float NewMax);

    // Delegate fired after NotifyMaxWeightChanged. UI and game logic may bind here.
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnMaxWeightChanged, float /*NewMax*/);
    FOnMaxWeightChanged OnMaxWeightChanged;

    // Delegate fired when CurrentWeight changes. Useful for encumbrance checks.
    DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCurrentWeightChanged,
        float /*OldWeight*/, float /*NewWeight*/);
    FOnCurrentWeightChanged OnCurrentWeightChanged;

    // UInventoryConstraint overrides
    virtual EInventoryMutationResult TestAdd(
        const UInventoryComponent& Inventory,
        FGameplayTag ItemTag, int32 Quantity) const override;

    virtual void OnItemAdded(const UInventoryComponent& Inventory,
                             FGameplayTag ItemTag, int32 Quantity) override;

    virtual void OnItemRemoved(const UInventoryComponent& Inventory,
                               FGameplayTag ItemTag, int32 Quantity) override;

private:
    float MaxWeight = 0.f;
    float CurrentWeight = 0.f;

    // Returns item weight via IItemDefinitionProvider. Returns 0 if no provider.
    float ResolveItemWeight(const UInventoryComponent& Inventory,
                            FGameplayTag ItemTag) const;
};
```

### TestAdd — Implementation

```cpp
EInventoryMutationResult UWeightConstraint::TestAdd(
    const UInventoryComponent& Inventory,
    FGameplayTag ItemTag, int32 Quantity) const
{
    const float ItemWeight = ResolveItemWeight(Inventory, ItemTag) * Quantity;
    const float NewWeight  = CurrentWeight + ItemWeight;

    if (WeightLimit > 0.f && NewWeight > WeightLimit)
        return EInventoryMutationResult::WeightLimitExceeded;

    if (MaxWeight > 0.f && NewWeight > MaxWeight)
        return EInventoryMutationResult::WeightExceeded;

    return EInventoryMutationResult::Success;
}
```

### NotifyMaxWeightChanged — Implementation

```cpp
void UWeightConstraint::NotifyMaxWeightChanged(float NewMax)
{
    // Clamp to hard cap if set.
    MaxWeight = (WeightLimit > 0.f) ? FMath::Min(NewMax, WeightLimit) : NewMax;
    OnMaxWeightChanged.Broadcast(MaxWeight);
    // Note: does NOT evict items. Grandfathering rule applies.
}
```

---

## USlotCountConstraint

```cpp
UCLASS(EditInlineNew, DefaultToInstanced,
       meta=(DisplayName="Slot Count Constraint"))
class GAMECORE_API USlotCountConstraint : public UInventoryConstraint
{
    GENERATED_BODY()
public:

    // Hard cap on total open slots. Designer-set. 0 = no hard cap.
    UPROPERTY(EditDefaultsOnly, Category="Slots", meta=(ClampMin=0))
    int32 SlotLimit = 0;

    // Current effective slot capacity. Set at BeginPlay or expanded by game logic.
    // Capped to SlotLimit if SlotLimit > 0.
    UPROPERTY(EditDefaultsOnly, Category="Slots", meta=(ClampMin=1))
    int32 MaxSlots = 20;

    int32 GetMaxSlots()     const { return MaxSlots; }
    int32 GetOccupiedSlots() const { return OccupiedSlots; }

    // Called by the game module to expand slot capacity (e.g. bag equipped).
    // Clamped to SlotLimit.
    void NotifyMaxSlotsChanged(int32 NewMax);

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnMaxSlotsChanged, int32);
    FOnMaxSlotsChanged OnMaxSlotsChanged;

    // UInventoryConstraint overrides
    virtual EInventoryMutationResult TestAdd(
        const UInventoryComponent& Inventory,
        FGameplayTag ItemTag, int32 Quantity) const override;

    virtual void OnItemAdded(const UInventoryComponent& Inventory,
                             FGameplayTag ItemTag, int32 Quantity) override;

    virtual void OnItemRemoved(const UInventoryComponent& Inventory,
                               FGameplayTag ItemTag, int32 Quantity) override;

private:
    int32 OccupiedSlots = 0;
};
```

### TestAdd — Implementation

```cpp
EInventoryMutationResult USlotCountConstraint::TestAdd(
    const UInventoryComponent& Inventory,
    FGameplayTag ItemTag, int32 Quantity) const
{
    // Check if the item can stack onto an existing slot.
    // If yes, no new slot is consumed — pass immediately.
    if (Inventory.CanStackOntoExistingSlot(ItemTag, Quantity))
        return EInventoryMutationResult::Success;

    // A new slot is required.
    const int32 NewOccupied = OccupiedSlots + 1;

    if (SlotLimit > 0 && NewOccupied > SlotLimit)
        return EInventoryMutationResult::SlotLimitExceeded;

    if (NewOccupied > MaxSlots)
        return EInventoryMutationResult::SlotsExceeded;

    return EInventoryMutationResult::Success;
}
```

> **Note:** `CanStackOntoExistingSlot` is a const query on `UInventoryComponent` that returns true if there is at least one existing slot for `ItemTag` with room remaining below `GetMaxStackSize(ItemTag)`.
