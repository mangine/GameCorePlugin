# UInventoryComponent

**Sub-page of:** [Inventory System](../Inventory%20System.md)
**File:** `Inventory/InventoryComponent.h / .cpp`
**Type:** `UActorComponent`
**Authority:** Server mutates; `FInventorySlotArray` replicates to owning client (`COND_OwnerOnly`)
**Implements:** `IPersistableComponent`

---

## Class Declaration

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UInventoryComponent
    : public UActorComponent
    , public IPersistableComponent
{
    GENERATED_BODY()
public:

    // ── Configuration ────────────────────────────────────────────────────

    // Optional slot layout. If null, behaves as UUnboundSlotLayout with no caps.
    UPROPERTY(EditDefaultsOnly, Instanced, Category="Inventory")
    TObjectPtr<UInventorySlotLayout> SlotLayout;

    // Zero, one, or both constraints may be set.
    UPROPERTY(EditDefaultsOnly, Instanced, Category="Inventory")
    TObjectPtr<UWeightConstraint> WeightConstraint;

    UPROPERTY(EditDefaultsOnly, Instanced, Category="Inventory")
    TObjectPtr<USlotCountConstraint> SlotCountConstraint;

    // Optional item definition bridge. Set at BeginPlay by the game module.
    // If null: weight = 0, stack size = 1 for all items.
    void SetItemDefinitionProvider(TScriptInterface<IItemDefinitionProvider> Provider);

    // ── Replication ──────────────────────────────────────────────────────

    // FastArray — only dirty slots replicate. COND_OwnerOnly.
    UPROPERTY(ReplicatedUsing=OnRep_Slots)
    FInventorySlotArray Slots;

    UFUNCTION()
    void OnRep_Slots();

    // Fired on client after any replicated slot update.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);
    UPROPERTY(BlueprintAssignable)
    FOnInventoryChanged OnInventoryChanged;

    // ── TryPlace (pure validation — never mutates) ────────────────────────

    // Test whether placing Quantity of ItemTag at a specific slot would succeed.
    // Safe to call on both server and client for prediction.
    UFUNCTION(BlueprintCallable, Category="Inventory")
    EInventoryMutationResult TryPlaceAt(
        int32 SlotIndex,
        FGameplayTag ItemTag,
        int32 Quantity,
        const FRequirementContext& Context) const;

    // Test auto-placement and return how much would fit.
    UFUNCTION(BlueprintCallable, Category="Inventory")
    FInventoryAutoPlaceResult TryPlaceAuto(
        FGameplayTag ItemTag,
        int32 Quantity,
        const FRequirementContext& Context) const;

    // ── Place (server authority — no requirement re-check) ────────────────

    // Place directly at slot. HasAuthority() must be true.
    // Returns false only on hard errors (invalid slot, missing layout).
    bool PlaceAt(int32 SlotIndex, FGameplayTag ItemTag, int32 Quantity,
                 const TArray<uint8>& InstanceData = {});

    // Auto-place using same greedy logic as TryPlaceAuto, without constraint checks.
    // Returns quantity actually placed.
    FInventoryAutoPlaceResult PlaceAuto(FGameplayTag ItemTag, int32 Quantity,
                                        const TArray<uint8>& InstanceData = {});

    // ── Remove / Drop / Dismantle ─────────────────────────────────────────

    // Remove Quantity from a slot. HasAuthority() required.
    // Fires Inventory.ItemRemoved GMS event.
    bool RemoveFromSlot(int32 SlotIndex, int32 Quantity);

    // Drop item from slot into the world. HasAuthority() required.
    // Fires Inventory.ItemDropped GMS event. Game module spawns the world actor.
    bool DropItem(int32 SlotIndex, int32 Quantity);

    // Remove and fire Inventory.ItemDismantled with full slot snapshot.
    // HasAuthority() required. Game module listens and grants resources.
    bool DismantleItem(int32 SlotIndex);

    // ── Queries (const — safe on client) ─────────────────────────────────

    const FInventorySlot* GetSlot(int32 SlotIndex) const;
    float GetCurrentWeight() const;
    int32 GetOccupiedSlotCount() const;
    bool CanStackOntoExistingSlot(FGameplayTag ItemTag, int32 Quantity) const;

    // ── IPersistableComponent ─────────────────────────────────────────────

    virtual FName GetPersistenceKey() const override { return TEXT("Inventory"); }
    virtual uint32 GetSchemaVersion() const override { return 1; }
    virtual void Serialize_Save(FArchive& Ar) override;
    virtual void Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void Migrate(FArchive& Ar, uint32 From, uint32 To) override;

protected:
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    TWeakInterfacePtr<IItemDefinitionProvider> ItemProvider;

    // Runs all active constraints. Returns first blocking result, or Success.
    EInventoryMutationResult RunConstraints(
        FGameplayTag ItemTag, int32 Quantity) const;

    // Core placement logic used by both TryPlaceAuto and PlaceAuto.
    // bDryRun = true for Try variant (no state mutation).
    FInventoryAutoPlaceResult ExecuteAutoPlace(
        FGameplayTag ItemTag, int32 Quantity,
        const FRequirementContext* Context,
        bool bDryRun,
        const TArray<uint8>& InstanceData = {});

    // Commits one slot mutation and calls all post-mutation hooks.
    void CommitSlotAdd(int32 SlotIndex, FGameplayTag ItemTag,
                       int32 Quantity, const TArray<uint8>& InstanceData);
    void CommitSlotRemove(int32 SlotIndex, int32 Quantity);

    int32 NextSlotIndex = 0;
};
```

---

## BeginPlay

```cpp
void UInventoryComponent::BeginPlay()
{
    Super::BeginPlay();

#if !UE_BUILD_SHIPPING
    if (WeightConstraint && !ItemProvider.IsValid())
    {
        ensureMsgf(false,
            TEXT("UInventoryComponent on %s has a WeightConstraint but no "
                 "IItemDefinitionProvider — weight will always be 0."),
            *GetOwner()->GetName());
    }
#endif
}
```

---

## TryPlaceAt

```cpp
EInventoryMutationResult UInventoryComponent::TryPlaceAt(
    int32 SlotIndex, FGameplayTag ItemTag,
    int32 Quantity, const FRequirementContext& Context) const
{
    if (Quantity <= 0) return EInventoryMutationResult::NothingToPlace;

    // Layout check.
    if (SlotLayout && !SlotLayout->HasSlot(SlotIndex))
        return EInventoryMutationResult::InvalidSlot;

    // Per-slot requirement check.
    if (SlotLayout)
    {
        EInventoryMutationResult SlotResult =
            SlotLayout->TestSlot(SlotIndex, ItemTag, Context, Slots);
        if (SlotResult != EInventoryMutationResult::Success)
            return SlotResult;
    }

    // Stack size check on the target slot.
    const FInventorySlot* Existing = Slots.FindByIndex(SlotIndex);
    if (Existing && !Existing->IsEmpty())
    {
        if (Existing->ItemTag != ItemTag)
            return EInventoryMutationResult::InvalidSlot; // Occupied by different item.

        const int32 MaxStack = ItemProvider.IsValid()
            ? ItemProvider->GetMaxStackSize(ItemTag) : 1;
        if (Existing->Quantity + Quantity > MaxStack)
            return EInventoryMutationResult::StackLimitExceeded;
    }

    // Constraint check (weight, slot count).
    return RunConstraints(ItemTag, Quantity);
}
```

---

## ExecuteAutoPlace (core shared logic)

```cpp
FInventoryAutoPlaceResult UInventoryComponent::ExecuteAutoPlace(
    FGameplayTag ItemTag, int32 Quantity,
    const FRequirementContext* Context,
    bool bDryRun,
    const TArray<uint8>& InstanceData)
{
    FInventoryAutoPlaceResult Out;
    Out.QuantityRemaining = Quantity;

    if (Quantity <= 0) return Out;

    // Build ordered candidate list from layout.
    TArray<int32> Candidates;
    if (SlotLayout)
        SlotLayout->GetAutoPlacementCandidates(ItemTag,
            Context ? *Context : FRequirementContext{}, Slots, Candidates);
    else
    {
        // No layout: partial stacks first, then open new slot (INDEX_NONE sentinel).
        for (const FInventorySlot& S : Slots.Items)
            if (!S.IsEmpty() && S.ItemTag == ItemTag) Candidates.Add(S.SlotIndex);
        for (const FInventorySlot& S : Slots.Items)
            if (S.IsEmpty()) Candidates.Add(S.SlotIndex);
        Candidates.Add(INDEX_NONE);
    }

    const int32 MaxStack = ItemProvider.IsValid()
        ? ItemProvider->GetMaxStackSize(ItemTag) : 1;

    for (int32 CandidateIdx : Candidates)
    {
        if (Out.QuantityRemaining <= 0) break;

        // INDEX_NONE = allocate new slot.
        int32 TargetSlot = CandidateIdx;
        int32 ExistingQty = 0;

        if (TargetSlot == INDEX_NONE)
        {
            // Check slot count constraint before opening a new slot.
            EInventoryMutationResult CR = RunConstraints(ItemTag, Out.QuantityRemaining);
            if (CR == EInventoryMutationResult::SlotLimitExceeded)
                break; // Hard cap — stop.
            if (CR == EInventoryMutationResult::SlotsExceeded)
                break; // Soft cap — stop auto-place.
            TargetSlot = NextSlotIndex; // Will be committed below.
        }
        else
        {
            const FInventorySlot* Existing = Slots.FindByIndex(TargetSlot);
            if (!Existing || (!Existing->IsEmpty() && Existing->ItemTag != ItemTag))
                continue;
            ExistingQty = Existing ? Existing->Quantity : 0;
        }

        // Weight constraint on this batch.
        const int32 Placeable = FMath::Min(Out.QuantityRemaining, MaxStack - ExistingQty);
        if (Placeable <= 0) continue;

        EInventoryMutationResult WR = RunConstraints(ItemTag, Placeable);
        if (WR == EInventoryMutationResult::WeightLimitExceeded)
            break; // Hard cap.
        if (WR == EInventoryMutationResult::WeightExceeded)
            break; // Soft cap — server could override, but TryPlace blocks here.

        if (!bDryRun)
            CommitSlotAdd(TargetSlot, ItemTag, Placeable, InstanceData);

        Out.QuantityPlaced    += Placeable;
        Out.QuantityRemaining -= Placeable;
    }

    Out.Result = (Out.QuantityPlaced == 0)
        ? EInventoryMutationResult::NothingToPlace
        : (Out.QuantityRemaining > 0)
            ? EInventoryMutationResult::PartialSuccess
            : EInventoryMutationResult::Success;

    return Out;
}
```

---

## CommitSlotAdd

```cpp
void UInventoryComponent::CommitSlotAdd(
    int32 SlotIndex, FGameplayTag ItemTag,
    int32 Quantity, const TArray<uint8>& InstanceData)
{
    // Allocate new slot if needed.
    FInventorySlot* Target = Slots.FindByIndex(SlotIndex);
    if (!Target)
    {
        FInventorySlot NewSlot;
        NewSlot.SlotIndex    = NextSlotIndex++;
        NewSlot.ItemTag      = ItemTag;
        NewSlot.Quantity     = Quantity;
        NewSlot.InstanceData = InstanceData;
        Slots.Items.Add(NewSlot);
        Slots.MarkItemDirty(Slots.Items.Last());
    }
    else
    {
        Target->Quantity += Quantity;
        Slots.MarkItemDirty(*Target);
    }

    // Notify constraints.
    if (WeightConstraint)   WeightConstraint->OnItemAdded(*this, ItemTag, Quantity);
    if (SlotCountConstraint) SlotCountConstraint->OnItemAdded(*this, ItemTag, Quantity);

    // GMS event.
    FInventoryItemAddedMessage Msg;
    Msg.OwnerActor = GetOwner();
    Msg.SlotIndex  = SlotIndex;
    Msg.ItemTag    = ItemTag;
    Msg.Quantity   = Quantity;
    UGameCoreEventSubsystem::Broadcast(
        GetWorld(), TAG_GameCoreEvent_Inventory_ItemAdded, Msg,
        EGameCoreEventScope::ServerOnly);

    // Audit.
    FGameCoreBackend::Audit(GetWorld(),
        TAG_Audit_Inventory_Add, GetOwner(), ItemTag.ToString(), Quantity);

    NotifyDirty(this);
}
```

---

## Persistence

```cpp
void UInventoryComponent::Serialize_Save(FArchive& Ar)
{
    int32 Count = Slots.Items.Num();
    Ar << Count;
    for (FInventorySlot& Slot : Slots.Items)
    {
        Ar << Slot.SlotIndex;
        Ar << Slot.ItemTag;
        Ar << Slot.Quantity;
        Ar << Slot.InstanceData;
    }
    Ar << NextSlotIndex;
}

void UInventoryComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    int32 Count;
    Ar << Count;
    Slots.Items.SetNum(Count);
    for (FInventorySlot& Slot : Slots.Items)
    {
        Ar << Slot.SlotIndex;
        Ar << Slot.ItemTag;
        Ar << Slot.Quantity;
        Ar << Slot.InstanceData;
    }
    Ar << NextSlotIndex;
}
```

> **Version 1 note:** The `InstanceData` blob is opaque — GameCore saves and loads it verbatim. The game module is responsible for migrating its own instance data schema via a separate versioning mechanism outside this component.

---

## GetLifetimeReplicatedProps

```cpp
void UInventoryComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UInventoryComponent, Slots, COND_OwnerOnly);
}
```
