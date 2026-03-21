# InventoryMessages

**File:** `Inventory/InventoryMessages.h`

All inventory GMS events are `ServerOnly` scope. Clients receive state changes via `FFastArraySerializer` replication and fire `OnInventoryChanged` locally — they do not receive GMS broadcasts.

Requirement watcher invalidation tags are defined alongside the events to keep the two in sync.

---

## Gameplay Tags

```ini
; DefaultGameplayTags.ini

; GMS event channels
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemAdded",          DevComment="Item added to an inventory slot")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemRemoved",        DevComment="Item removed from a slot")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemDropped",        DevComment="Item dropped into the world")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemDismantled",     DevComment="Item dismantled; game module grants resources")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.WeightChanged",      DevComment="Current inventory weight changed")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ConstraintViolated", DevComment="A mutation was blocked by a constraint")

; Requirement watcher invalidation tags
+GameplayTagList=(Tag="RequirementEvent.Inventory.ItemAdded",       DevComment="Watcher: item added — re-evaluate HasItem requirements")
+GameplayTagList=(Tag="RequirementEvent.Inventory.ItemRemoved",     DevComment="Watcher: item removed — re-evaluate HasItem requirements")

; Audit tags
+GameplayTagList=(Tag="Audit.Inventory.Add",       DevComment="Item added audit record")
+GameplayTagList=(Tag="Audit.Inventory.Remove",    DevComment="Item removed audit record")
+GameplayTagList=(Tag="Audit.Inventory.Drop",      DevComment="Item dropped audit record")
+GameplayTagList=(Tag="Audit.Inventory.Dismantle", DevComment="Item dismantled audit record")
```

---

## FInventoryItemAddedMessage

**Channel:** `GameCoreEvent.Inventory.ItemAdded`
**Scope:** `ServerOnly`
**Origin:** `UInventoryComponent::CommitSlotAdd`

Also fires `RequirementEvent.Inventory.ItemAdded` via the watcher subsystem so `URequirement_HasItem` is re-evaluated.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FInventoryItemAddedMessage
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<AActor> OwnerActor = nullptr;
    UPROPERTY() int32 SlotIndex = INDEX_NONE;
    UPROPERTY() FGameplayTag ItemTag;
    UPROPERTY() int32 Quantity = 0;
};
```

---

## FInventoryItemRemovedMessage

**Channel:** `GameCoreEvent.Inventory.ItemRemoved`
**Scope:** `ServerOnly`
**Origin:** `UInventoryComponent::CommitSlotRemove`

Also fires `RequirementEvent.Inventory.ItemRemoved`.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FInventoryItemRemovedMessage
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<AActor> OwnerActor = nullptr;
    UPROPERTY() int32 SlotIndex = INDEX_NONE;
    UPROPERTY() FGameplayTag ItemTag;
    UPROPERTY() int32 QuantityRemoved  = 0;
    UPROPERTY() int32 QuantityRemaining = 0; // 0 = slot emptied
};
```

---

## FInventoryItemDroppedMessage

**Channel:** `GameCoreEvent.Inventory.ItemDropped`
**Scope:** `ServerOnly`
**Origin:** `UInventoryComponent::DropItem`

The game module listens to this event and spawns a world pickup actor.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FInventoryItemDroppedMessage
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<AActor> OwnerActor = nullptr;
    // Full slot snapshot at the moment of drop. InstanceData preserved.
    UPROPERTY() FInventorySlot DroppedSlot;
    // World location hint. Typically GetOwner()->GetActorLocation().
    UPROPERTY() FVector DropLocation = FVector::ZeroVector;
};
```

---

## FInventoryItemDismantledMessage

**Channel:** `GameCoreEvent.Inventory.ItemDismantled`
**Scope:** `ServerOnly`
**Origin:** `UInventoryComponent::DismantleItem`

The game module uses `DismantledSlot` (including `InstanceData`) to determine and grant dismantle rewards.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FInventoryItemDismantledMessage
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<AActor> OwnerActor = nullptr;
    // Full slot snapshot. Game module owns InstanceData schema.
    UPROPERTY() FInventorySlot DismantledSlot;
};
```

---

## FInventoryWeightChangedMessage

**Channel:** `GameCoreEvent.Inventory.WeightChanged`
**Scope:** `ServerOnly`
**Origin:** `UWeightConstraint::OnItemAdded` / `OnItemRemoved`

> **Note:** `UWeightConstraint` broadcasts this event via the Event Bus after updating `CurrentWeight`. The component does not need to broadcast it separately.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FInventoryWeightChangedMessage
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<AActor> OwnerActor = nullptr;
    UPROPERTY() float CurrentWeight = 0.f;
    UPROPERTY() float MaxWeight     = 0.f;
    UPROPERTY() float WeightLimit   = 0.f;
};
```

---

## FInventoryConstraintViolatedMessage

**Channel:** `GameCoreEvent.Inventory.ConstraintViolated`
**Scope:** `ServerOnly`
**Origin:** Game module RPC handler, after calling `TryPlace*` and receiving a blocking result.

> **Important:** This event is **not** broadcast from inside `TryPlaceAt` or `TryPlaceAuto`. Those methods are callable on the client for prediction — broadcasting a `ServerOnly` event from the client would be incorrect. The game module's server RPC handler is responsible for broadcasting this event when blocking results are received, before returning to the caller.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FInventoryConstraintViolatedMessage
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<AActor> OwnerActor = nullptr;
    UPROPERTY() FGameplayTag ItemTag;
    UPROPERTY() int32 Quantity = 0;
    UPROPERTY() EInventoryMutationResult ViolationResult =
        EInventoryMutationResult::NothingToPlace;
};
```
