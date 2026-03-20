# Inventory System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Inventory System manages item storage for any actor. It is server-authoritative, replication-ready, and constraint-driven. GameCore owns the container, layout, and constraint logic. Item definitions, item weight values, and all game-specific logic live in the game module.

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
- Partial pickups are supported: if only N of M items fit, N are picked up and M-N remain in the source.
- All mutations are audited via `FGameCoreBackend::Audit()`.
- `UInventoryComponent` implements `IPersistableComponent`.
- Slots replicate to the owning client only via `FFastArraySerializer`.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| No `UItemDefinition` in GameCore | Item data is deeply game-specific. GameCore knows items only by tag. |
| `IItemDefinitionProvider` interface | Decouples inventory logic (stack size, weight) from item data without a hard game-module dependency. Null fallback degrades gracefully. |
| `FFastArraySerializer` for slots | Per-slot dirty marking — only changed slots replicate. Full-array replication on every change is unacceptable in an MMORPG. |
| `SlotIndex` as stable replication key | Integer index survives layout changes and is future-proof for matrix (which adds `FIntPoint Position` without removing `SlotIndex`). |
| Two-tier weight and slot caps | `MaxWeight`/`MaxSlots` are game-driven and can be buffed. `WeightLimit`/`SlotLimit` are designer hard caps that cannot be exceeded regardless of buffs. |
| `NotifyMaxWeightChanged` delegate pattern | GAS attribute delegate wires directly to this — no GAS dependency in GameCore. |
| `TryPlace` never mutates | Pure validation pass. Server calls `Place` variants with authority after a successful `TryPlace` or unconditionally for server-driven mutations. |
| Greedy stacking in auto-placement | Fill partial stacks before opening new slots. Minimises fragmentation by default. |
| Tagged slots tried before general slots in auto-placement | Items with a matching slot tag are placed in the most semantically correct location first. |
| Grandfathering of invalid items | Item definition or tag changes are not the player's fault. Evicting would feel punishing. Re-validation deferred to next player interaction. |
| Drop fires GMS event only | GameCore has no concept of world pickups. The game module spawns the actor. |
| Dismantle fires dedicated GMS event | `ItemRemoved` alone is insufficient — the game module needs slot snapshot to grant resources. |
| Audit on all mutations | Item duplication is the most common MMORPG exploit. All add/remove/drop/dismantle events must be traceable. |
| `InstanceData` as opaque `TArray<uint8>` | Per-item instance state (durability, enchants, owner name) is game-specific. GameCore stores the blob; game module owns the schema. |

---

## System Units

| Unit | Class | Role |
|---|---|---|
| Core types | `InventoryTypes.h` | Enums, structs, `FInventorySlot`, `FInventorySlotArray` |
| Item provider interface | `IItemDefinitionProvider` | Optional bridge for item weight and stack size |
| Constraint base | `UInventoryConstraint` | Abstract instanced UObject — strategy pattern |
| Weight constraint | `UWeightConstraint` | Two-tier weight cap, GAS-bridgeable |
| Slot count constraint | `USlotCountConstraint` | Two-tier slot cap |
| Slot layout base | `UInventorySlotLayout` | Abstract layout — owns slot query/placement logic |
| Tagged slot layout | `UTaggedSlotLayout` | Named slots with optional `URequirementList` guards |
| Unbound slot layout | `UUnboundSlotLayout` | Open slot pool — no per-slot identity |
| Inventory component | `UInventoryComponent` | State owner, replication, persistence, mutation entry points |
| GMS events | `InventoryMessages.h` | All broadcast message structs |

---

## Where It Lives

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

---

## Key Flows

### Auto-Place (client-initiated pickup)
```
[Client presses pickup]
    ServerRPC_PickupItem(ItemTag, Quantity, SourceActor)
    │
    ├── Server validates source actor has the item
    ├── Calls TryPlaceAuto(ItemTag, Quantity, Context)
    │   ├── Run all constraints → accumulate max placeable quantity
    │   ├── Find partial stacks of same ItemTag → fill up to GetMaxStackSize()
    │   ├── Try tagged slots matching ItemTag (requirement-checked)
    │   ├── Try general/unbound slots
    │   └── Return FInventoryAutoPlaceResult { Result, QuantityPlaced, QuantityRemaining }
    │
    ├── If QuantityPlaced > 0 → call PlaceAuto (authority, no re-check)
    │   ├── Mutate FInventorySlotArray
    │   ├── MarkItemDirty per modified slot → replication fires
    │   ├── Broadcast GMS Inventory.ItemAdded
    │   └── FGameCoreBackend::Audit(TAG_Audit_Inventory_Add)
    │
    └── If QuantityRemaining > 0 → notify source to retain remainder
```

### Drop
```
[Server initiates drop (via RPC or game logic)]
    UInventoryComponent::DropItem(SlotIndex, Quantity)
    │
    ├── Validate slot exists, HasAuthority
    ├── Capture FInventorySlot snapshot
    ├── Reduce quantity or remove slot
    ├── MarkItemDirty → replication fires
    ├── Broadcast GMS Inventory.ItemDropped (snapshot + actor ref)
    ├── FGameCoreBackend::Audit(TAG_Audit_Inventory_Drop)
    └── [Game module listens to GMS event, spawns world pickup actor]
```

### Dismantle
```
[Server initiates dismantle]
    UInventoryComponent::DismantleItem(SlotIndex)
    │
    ├── Validate slot exists, HasAuthority
    ├── Capture FInventorySlot snapshot
    ├── Remove slot entirely
    ├── MarkItemDirty → replication fires
    ├── Broadcast GMS Inventory.ItemDismantled (full snapshot)
    ├── FGameCoreBackend::Audit(TAG_Audit_Inventory_Dismantle)
    └── [Game module listens, grants component resources]
```

---

## Integration Guide

See [Integration Guide](Inventory%20System/Integration%20Guide.md) for full setup, GAS wiring, and usage samples.

---

## Sub-pages

- [InventoryTypes & IItemDefinitionProvider](Inventory%20System/InventoryTypes.md)
- [UInventoryConstraint, UWeightConstraint, USlotCountConstraint](Inventory%20System/Constraints.md)
- [UInventorySlotLayout, UTaggedSlotLayout, UUnboundSlotLayout](Inventory%20System/SlotLayouts.md)
- [UInventoryComponent](Inventory%20System/UInventoryComponent.md)
- [GMS Events](Inventory%20System/GMS%20Events.md)
- [Integration Guide](Inventory%20System/Integration%20Guide.md)
