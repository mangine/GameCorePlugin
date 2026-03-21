# Slot Layouts

**Files:** `Inventory/Layouts/InventorySlotLayout.h/.cpp`, `TaggedSlotLayout.h/.cpp`, `UnboundSlotLayout.h/.cpp`

The slot layout answers one question: **where can an item go?** It is queried during both `TryPlace` and auto-placement. It has no authority over constraints — weight and slot count are evaluated separately by `UInventoryComponent`.

`UInventoryComponent` holds a single `UInventorySlotLayout*` instance (optional). If null, the component behaves as an unbound layout with no slot-level requirements.

---

## UInventorySlotLayout — Abstract Base

```cpp
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UInventorySlotLayout : public UObject
{
    GENERATED_BODY()
public:

    // Returns Success if the given slot accepts this item under this context.
    // Must be const — no side effects.
    virtual EInventoryMutationResult TestSlot(
        int32 SlotIndex,
        FGameplayTag ItemTag,
        const FRequirementContext& Context,
        const FInventorySlotArray& Slots) const PURE_VIRTUAL(
            UInventorySlotLayout::TestSlot,
            return EInventoryMutationResult::InvalidSlot;);

    // Returns an ordered list of candidate slot indices for auto-placement.
    // Order matters: UInventoryComponent iterates this list greedily.
    // The layout does NOT filter for constraints — that is the component's job.
    virtual void GetAutoPlacementCandidates(
        FGameplayTag ItemTag,
        const FRequirementContext& Context,
        const FInventorySlotArray& Slots,
        TArray<int32>& OutSlotIndices) const PURE_VIRTUAL(
            UInventorySlotLayout::GetAutoPlacementCandidates, );

    // Returns true if this layout defines a slot at SlotIndex.
    virtual bool HasSlot(int32 SlotIndex) const PURE_VIRTUAL(
        UInventorySlotLayout::HasSlot, return false;);

    // Total number of defined slots in this layout.
    virtual int32 GetTotalSlotCount() const PURE_VIRTUAL(
        UInventorySlotLayout::GetTotalSlotCount, return 0;);
};
```

---

## FTaggedSlotDefinition

Author-time definition for one named slot. Used by `UTaggedSlotLayout`.

```cpp
USTRUCT()
struct GAMECORE_API FTaggedSlotDefinition
{
    GENERATED_BODY()

    // Stable index. Must be unique within a layout. Assigned by the designer.
    UPROPERTY(EditDefaultsOnly)
    int32 SlotIndex = INDEX_NONE;

    // Semantic tag for this slot (e.g. Inventory.Slot.Helmet, Inventory.Slot.Pouch).
    UPROPERTY(EditDefaultsOnly)
    FGameplayTag SlotTag;

    // Optional requirement list. If null, no restriction beyond slot tag matching.
    // Evaluated server-side during TryPlace and auto-placement.
    UPROPERTY(EditDefaultsOnly, Instanced)
    TObjectPtr<URequirementList> Requirements;

    // If true, auto-placement will place any item here, not only items whose
    // item tag matches SlotTag. Tagged-slot-first ordering still applies.
    UPROPERTY(EditDefaultsOnly)
    bool bAcceptsGeneralItems = true;
};
```

---

## UTaggedSlotLayout

```cpp
UCLASS(EditInlineNew, DefaultToInstanced,
       meta=(DisplayName="Tagged Slot Layout"))
class GAMECORE_API UTaggedSlotLayout : public UInventorySlotLayout
{
    GENERATED_BODY()
public:

    UPROPERTY(EditDefaultsOnly, Category="Slots")
    TArray<FTaggedSlotDefinition> SlotDefinitions;

    virtual EInventoryMutationResult TestSlot(
        int32 SlotIndex, FGameplayTag ItemTag,
        const FRequirementContext& Context,
        const FInventorySlotArray& Slots) const override;

    virtual void GetAutoPlacementCandidates(
        FGameplayTag ItemTag, const FRequirementContext& Context,
        const FInventorySlotArray& Slots,
        TArray<int32>& OutSlotIndices) const override;

    virtual bool HasSlot(int32 SlotIndex) const override;
    virtual int32 GetTotalSlotCount() const override;

protected:
    // Built at PostLoad / BeginPlay from SlotDefinitions.
    // Key = SlotIndex, Value = pointer into SlotDefinitions array.
    TMap<int32, const FTaggedSlotDefinition*> IndexMap;

    virtual void PostLoad() override; // Rebuild IndexMap
};
```

### TestSlot

```cpp
EInventoryMutationResult UTaggedSlotLayout::TestSlot(
    int32 SlotIndex, FGameplayTag ItemTag,
    const FRequirementContext& Context,
    const FInventorySlotArray& Slots) const
{
    const FTaggedSlotDefinition* Def = IndexMap.FindRef(SlotIndex);
    if (!Def) return EInventoryMutationResult::InvalidSlot;

    // Item must match the slot tag, unless bAcceptsGeneralItems is true.
    if (Def->SlotTag.IsValid() && !ItemTag.MatchesTag(Def->SlotTag))
    {
        if (!Def->bAcceptsGeneralItems)
            return EInventoryMutationResult::RequirementFailed;
    }

    // Evaluate requirement list if present.
    if (Def->Requirements)
    {
        if (!Def->Requirements->EvaluateAll(Context).bPassed)
            return EInventoryMutationResult::RequirementFailed;
    }

    return EInventoryMutationResult::Success;
}
```

### GetAutoPlacementCandidates

Auto-placement order:
1. Tagged slots whose `SlotTag` matches `ItemTag` (semantically correct first).
2. Tagged slots with `bAcceptsGeneralItems = true` that pass requirements.

Partial-stack prioritisation over empty slots is handled by `UInventoryComponent`, not the layout.

```cpp
void UTaggedSlotLayout::GetAutoPlacementCandidates(
    FGameplayTag ItemTag, const FRequirementContext& Context,
    const FInventorySlotArray& Slots, TArray<int32>& OutSlotIndices) const
{
    // Pass 1: slots whose tag matches the item tag.
    for (const FTaggedSlotDefinition& Def : SlotDefinitions)
    {
        if (Def.SlotTag.IsValid() && ItemTag.MatchesTag(Def.SlotTag))
        {
            if (!Def.Requirements || Def.Requirements->EvaluateAll(Context).bPassed)
                OutSlotIndices.Add(Def.SlotIndex);
        }
    }

    // Pass 2: general slots (no tag match required, bAcceptsGeneralItems = true).
    for (const FTaggedSlotDefinition& Def : SlotDefinitions)
    {
        if (Def.bAcceptsGeneralItems && !OutSlotIndices.Contains(Def.SlotIndex))
        {
            if (!Def.Requirements || Def.Requirements->EvaluateAll(Context).bPassed)
                OutSlotIndices.Add(Def.SlotIndex);
        }
    }
}
```

---

## UUnboundSlotLayout

Open pool — no named slots, no per-slot requirements. Slot count is governed entirely by `USlotCountConstraint`. Default layout for general-purpose bags.

```cpp
UCLASS(EditInlineNew, DefaultToInstanced,
       meta=(DisplayName="Unbound Slot Layout"))
class GAMECORE_API UUnboundSlotLayout : public UInventorySlotLayout
{
    GENERATED_BODY()
public:

    // Maximum slots this layout can ever represent.
    // Bounds the slot index space for UI rendering.
    // 0 = unlimited (rely solely on USlotCountConstraint).
    UPROPERTY(EditDefaultsOnly, Category="Slots", meta=(ClampMin=0))
    int32 MaxLayoutSlots = 0;

    virtual EInventoryMutationResult TestSlot(
        int32 SlotIndex, FGameplayTag ItemTag,
        const FRequirementContext& Context,
        const FInventorySlotArray& Slots) const override;

    virtual void GetAutoPlacementCandidates(
        FGameplayTag ItemTag, const FRequirementContext& Context,
        const FInventorySlotArray& Slots,
        TArray<int32>& OutSlotIndices) const override;

    virtual bool HasSlot(int32 SlotIndex) const override;
    virtual int32 GetTotalSlotCount() const override;
};
```

### TestSlot

```cpp
EInventoryMutationResult UUnboundSlotLayout::TestSlot(
    int32 SlotIndex, FGameplayTag /*ItemTag*/,
    const FRequirementContext& /*Context*/,
    const FInventorySlotArray& Slots) const
{
    // Any slot index is valid as long as it exists in the current slot array.
    if (Slots.FindByIndex(SlotIndex) == nullptr)
        return EInventoryMutationResult::InvalidSlot;
    return EInventoryMutationResult::Success;
}
```

### GetAutoPlacementCandidates

Returns all existing slot indices with partial stacks of the same item first, then empty slots, then a `INDEX_NONE` sentinel to signal "open a new slot".

```cpp
void UUnboundSlotLayout::GetAutoPlacementCandidates(
    FGameplayTag ItemTag, const FRequirementContext& /*Context*/,
    const FInventorySlotArray& Slots, TArray<int32>& OutSlotIndices) const
{
    // Partial stacks first.
    for (const FInventorySlot& Slot : Slots.Items)
        if (!Slot.IsEmpty() && Slot.ItemTag == ItemTag)
            OutSlotIndices.Add(Slot.SlotIndex);

    // Empty slots second.
    for (const FInventorySlot& Slot : Slots.Items)
        if (Slot.IsEmpty())
            OutSlotIndices.Add(Slot.SlotIndex);

    // Sentinel: component will allocate a new slot if constraints allow.
    OutSlotIndices.Add(INDEX_NONE);
}
```

> **Matrix extension note:** A future `UMatrixSlotLayout` subclass overrides `GetAutoPlacementCandidates` with a 2D bin-packing pass. `FInventorySlot` gains `FIntPoint Position` and `FIntPoint Size`. No changes to `UInventoryComponent` or constraints are required.
