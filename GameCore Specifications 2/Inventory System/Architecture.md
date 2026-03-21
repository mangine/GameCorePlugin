# Inventory System — Architecture

**Part of:** GameCore Plugin | **UE Version:** 5.7 | **Status:** Active Specification

The Inventory System manages item storage for any actor. It is server-authoritative, replication-ready, and constraint-driven. GameCore owns the container, layout, and constraint logic. Item definitions, item weight values, and all game-specific logic live in the game module.

---

## Dependencies

### Unreal Engine Modules
| Module | Reason |
|---|---|
| `GameplayTags` | Items identified by `FGameplayTag`; slot tags; event channels |
| `NetCore` | `FFastArraySerializer` for per-slot delta replication |
| `Engine` | `UActorComponent`, `UObject`, `UWorldSubsystem` |

### GameCore Plugin Systems
| System | Reason |
|---|---|
| **Event Bus** (`UGameCoreEventSubsystem`) | All mutations broadcast GMS messages (`ServerOnly` scope) |
| **Serialization System** (`IPersistableComponent`, `UPersistenceRegistrationComponent`) | Inventory state is persisted per-actor |
| **Requirement System** (`URequirementList`, `FRequirementContext`) | Per-slot placement guards in `UTaggedSlotLayout` |
| **Backend** (`FGameCoreBackend::Audit`) | Every add/remove/drop/dismantle is audited |

### Optional (game module)
| Dependency | Reason |
|---|---|
| GAS (`UAbilitySystemComponent`) | Bridges `MaxCarryWeight` attribute → `UWeightConstraint::NotifyMaxWeightChanged` — **no GAS include in GameCore** |
| `IItemDefinitionProvider` | Supplies item weight and stack size — implemented by the game module |

---

## Requirements

- Items are identified solely by `FGameplayTag`. No `UItemDefinition` base class exists in GameCore.
- The inventory must support stacking, partial pickup, dropping, and dismantling.
- Two independent constraint types are supported: **weight** and **slot count**. Each may be used alone or together.
- Weight has two tiers: a soft cap (`MaxWeight`, GAS-driven) and a hard cap (`WeightLimit`, designer-set). Slot count mirrors this with `MaxSlots` / `SlotLimit`.
- Slot layout is pluggable. Two layouts ship with GameCore: `UTaggedSlotLayout` and `UUnboundSlotLayout`. Matrix layout is deferred but the data model is designed to accommodate it.
- All mutations are **server-authoritative**. Clients call Server RPCs; the server validates, mutates, and replicates.
- `TryPlace` variants test constraints and return a result enum without mutating state. `Place` variants are authority-only and bypass requirement checks.
- Items already placed that become invalid (requirement changes, weight cap drops) are **never evicted** (grandfathering). The item is only re-evaluated when the player next attempts to move it.
- Partial pickups are supported: if only N of M items fit, N are picked up and M−N remain in the source.
- All mutations are audited via `FGameCoreBackend::Audit()`.
- `UInventoryComponent` implements `IPersistableComponent`.
- Slots replicate to the owning client only via `FFastArraySerializer`.

---

## Features

- **Stacking:** Greedy partial-stack fill before opening new slots; configurable `MaxStackSize` via `IItemDefinitionProvider`.
- **Partial pickup:** `FInventoryAutoPlaceResult` carries `QuantityPlaced` and `QuantityRemaining`; source retains remainder.
- **Drop:** Server removes item and broadcasts `ItemDropped`; game module spawns world pickup actor.
- **Dismantle:** Server removes item and broadcasts `ItemDismantled` with full slot snapshot; game module grants resources.
- **Tagged slots:** Named equipment slots with optional `URequirementList` guards (e.g. helmet slot requires helmet-type item).
- **Unbound slots:** Open bag pool; slot count governed by `USlotCountConstraint`.
- **GAS weight bridge:** `UWeightConstraint::NotifyMaxWeightChanged` called from GAS attribute delegate — zero GAS dependency in GameCore.
- **Expandable bags:** `USlotCountConstraint::NotifyMaxSlotsChanged` expands capacity at runtime (e.g. equipping a bag).
- **Per-item instance data:** Opaque `TArray<uint8>` blob per slot; GameCore stores/restores verbatim; game module owns schema.
- **Client prediction support:** `TryPlace*` variants are callable on client for local UI feedback before RPC round-trip.
- **Matrix layout ready:** `FInventorySlot::SlotIndex` is the stable replication key; future `FIntPoint Position/Size` fields can be appended without breaking saves.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| No `UItemDefinition` in GameCore | Item data is deeply game-specific. GameCore knows items only by tag. |
| `IItemDefinitionProvider` interface | Decouples inventory logic from item data without a hard game-module dependency. Null fallback degrades gracefully. |
| `FFastArraySerializer` for slots | Per-slot dirty marking — only changed slots replicate. Full-array replication on every change is unacceptable in an MMORPG. |
| `SlotIndex` as stable replication key | Integer index survives layout changes and is future-proof for matrix layout. |
| Two-tier weight and slot caps | `MaxWeight`/`MaxSlots` are game-driven and can be buffed. `WeightLimit`/`SlotLimit` are designer hard caps that cannot be exceeded regardless of buffs. |
| `NotifyMaxWeightChanged` delegate pattern | GAS attribute delegate wires directly to this — no GAS dependency in GameCore. |
| `TryPlace` never mutates | Pure validation pass. Server calls `Place` variants with authority after a successful `TryPlace` or unconditionally for server-driven mutations. |
| Greedy stacking in auto-placement | Fill partial stacks before opening new slots. Minimises fragmentation by default. |
| Tagged slots tried before general slots | Items with a matching slot tag are placed in the most semantically correct location first. |
| Grandfathering of invalid items | Item definition or tag changes are not the player's fault. Evicting would feel punishing. Re-validation deferred to next player interaction. |
| Drop fires GMS event only | GameCore has no concept of world pickups. The game module spawns the actor. |
| Dismantle fires dedicated GMS event | `ItemRemoved` alone is insufficient — the game module needs the slot snapshot to grant resources. |
| Audit on all mutations | Item duplication is the most common MMORPG exploit. All add/remove/drop/dismantle events must be traceable. |
| `InstanceData` as opaque `TArray<uint8>` | Per-item instance state (durability, enchants) is game-specific. GameCore stores the blob; game module owns the schema. |
| `FInventoryConstraintViolatedMessage` not used for blocking | The result enum already blocks. The GMS message is for UI feedback and server-side analytics only. |

---

## Logic Flow

### Auto-Place (client-initiated pickup)
```
[Client presses pickup]
    ServerRPC_PickupItem(ItemTag, Quantity, SourceActor)   [game module RPC]
    │
    ├── Server validates source actor has the item
    ├── UInventoryComponent::TryPlaceAuto(ItemTag, Quantity, Context)
    │   ├── Layout::GetAutoPlacementCandidates → ordered slot list
    │   │     (partial stacks first, matching tagged slots first)
    │   ├── For each candidate:
    │   │   ├── RunConstraints (weight + slot count)
    │   │   └── Check stack size headroom
    │   └── Returns FInventoryAutoPlaceResult { Result, QuantityPlaced, QuantityRemaining }
    │
    ├── If QuantityPlaced > 0:
    │   ├── UInventoryComponent::PlaceAuto (authority, no re-check)
    │   │   ├── CommitSlotAdd per modified slot
    │   │   │   ├── Mutate FInventorySlotArray
    │   │   │   ├── MarkItemDirty → replication fires (COND_OwnerOnly)
    │   │   │   ├── Notify constraints (OnItemAdded)
    │   │   │   ├── Broadcast GameCoreEvent.Inventory.ItemAdded (ServerOnly)
    │   │   │   └── FGameCoreBackend::Audit(Audit.Inventory.Add)
    │   │   └── Returns FInventoryAutoPlaceResult
    │   └── Source: ConsumeQuantity(QuantityPlaced)
    │
    └── If QuantityRemaining > 0 → ClientRPC_PickupPartial(QuantityRemaining)
```

### Drop
```
[Server] UInventoryComponent::DropItem(SlotIndex, Quantity)
    ├── Validate slot exists, HasAuthority()
    ├── CommitSlotRemove(SlotIndex, Quantity)
    │   ├── Reduce/clear slot quantity
    │   ├── MarkItemDirty → replication fires
    │   ├── Notify constraints (OnItemRemoved)
    │   └── Broadcast GameCoreEvent.Inventory.ItemRemoved (ServerOnly)
    ├── Broadcast GameCoreEvent.Inventory.ItemDropped (snapshot + location)
    ├── FGameCoreBackend::Audit(Audit.Inventory.Drop)
    └── [Game module listens → spawns AMyPickupActor]
```

### Dismantle
```
[Server] UInventoryComponent::DismantleItem(SlotIndex)
    ├── Validate slot exists, HasAuthority()
    ├── Capture FInventorySlot snapshot
    ├── CommitSlotRemove(SlotIndex, full quantity)
    ├── Broadcast GameCoreEvent.Inventory.ItemDismantled (full snapshot)
    ├── FGameCoreBackend::Audit(Audit.Inventory.Dismantle)
    └── [Game module listens → reads InstanceData → grants resources]
```

### TryPlaceAt (prediction / UI validation)
```
[Client or Server] UInventoryComponent::TryPlaceAt(SlotIndex, ItemTag, Qty, Context)
    ├── Quantity <= 0 → NothingToPlace
    ├── Layout::HasSlot(SlotIndex) → InvalidSlot if false
    ├── Layout::TestSlot(SlotIndex, ItemTag, Context, Slots)
    │   └── RequirementList::EvaluateAll(Context) → RequirementFailed if fails
    ├── Existing slot item tag mismatch → InvalidSlot
    ├── ExistingQty + Qty > MaxStackSize → StackLimitExceeded
    └── RunConstraints(ItemTag, Qty) → weight / slot result
```

---

## System Units

| Unit | Class | Role |
|---|---|---|
| Core types | `InventoryTypes.h` | Enums, structs, `FInventorySlot`, `FInventorySlotArray`, `FInventoryAutoPlaceResult` |
| Event messages | `InventoryMessages.h` | All GMS broadcast message structs |
| Item provider interface | `IItemDefinitionProvider` | Optional bridge for item weight and stack size |
| Constraint base | `UInventoryConstraint` | Abstract instanced UObject — strategy pattern |
| Weight constraint | `UWeightConstraint` | Two-tier weight cap, GAS-bridgeable |
| Slot count constraint | `USlotCountConstraint` | Two-tier slot cap |
| Slot layout base | `UInventorySlotLayout` | Abstract layout — owns slot query/placement logic |
| Tagged slot layout | `UTaggedSlotLayout` | Named slots with optional `URequirementList` guards |
| Unbound slot layout | `UUnboundSlotLayout` | Open slot pool — no per-slot identity |
| Inventory component | `UInventoryComponent` | State owner, replication, persistence, mutation entry points |

---

## Known Issues

1. **`TryPlaceAuto` weight check is per-batch, not per-candidate:** The current `ExecuteAutoPlace` calls `RunConstraints` with `Placeable` quantity per candidate slot. If weight is fractional and rounding causes the check to pass for a smaller batch but the total accumulated weight slightly overflows `MaxWeight`, the component will be marginally over the soft cap. This is acceptable for gameplay but worth noting for precision-sensitive designs.

2. **`USlotCountConstraint::TestAdd` calls `CanStackOntoExistingSlot` which is a full scan:** For large inventories (hundreds of slots) this is O(n). Acceptable for MMORPG bag sizes (≤200 slots) but should be profiled if exotic use cases push counts higher.

3. **`FInventoryConstraintViolatedMessage` is never broadcast in the current `CommitSlotAdd` path:** The spec states it should be fired from `TryPlaceAt/TryPlaceAuto` when a blocking result is returned. The broadcast must be added at the RPC entry point in the game module (not inside `TryPlace*`), because `TryPlace*` is called for client-side prediction and broadcasting a server-only event from a client call is incorrect.

4. **`InstanceData` migration is game module responsibility:** GameCore saves the blob verbatim. If the game module changes its `InstanceData` schema between versions, it must handle deserialization of old blobs itself. There is no migration hook for `InstanceData` in GameCore.

5. **`PostReplicatedChange` in `FInventorySlotArray` has no direct reference to `UInventoryComponent`:** The implementation must use the standard `FFastArraySerializer` outer-object pattern (pass `this` from `GetLifetimeReplicatedProps` registration) to correctly call `OnInventoryChanged`.

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Inventory/
            ├── InventoryTypes.h
            ├── InventoryMessages.h
            ├── IItemDefinitionProvider.h
            ├── Constraints/
            │   ├── InventoryConstraint.h / .cpp
            │   ├── WeightConstraint.h / .cpp
            │   └── SlotCountConstraint.h / .cpp
            ├── Layouts/
            │   ├── InventorySlotLayout.h / .cpp
            │   ├── TaggedSlotLayout.h / .cpp
            │   └── UnboundSlotLayout.h / .cpp
            └── InventoryComponent.h / .cpp
```
