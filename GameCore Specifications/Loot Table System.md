# Loot Table System

Server-side only. Produces `TArray<FLootReward>` from a `ULootTable` data asset given a roll context. Callers decide how to fulfill rewards — the loot system never touches inventory, currency, or progression directly.

---

## Design Principles

- **Pure producer.** `ULootRollerSubsystem::RunLootTable` returns results. Fulfillment is the caller's responsibility.
- **Server-only authority.** All rolling happens on the server. Never called from client code.
- **No forced dependencies.** GameCore does not reference inventory, currency, or any game-specific system. `FLootReward` carries a `RewardType` tag and a soft asset reference — the game layer routes from there.
- **Open asset contract.** Reward assets opt in to loot compatibility via `ILootRewardable` — a marker interface. No base class constraint. Any asset type in any system can be slotted into a loot entry by implementing the interface.
- **Audit always fires.** Every roll is recorded via `FGameCoreBackend::GetAudit()` regardless of result.
- **Deterministic when seeded.** An optional seed in `FLootRollContext` enables CS reproduction of any historical roll.

---

## Selection Model

`ELootTableType` controls how entries are selected. Only `Threshold` is implemented.

```cpp
UENUM(BlueprintType)
enum class ELootTableType : uint8
{
    // Roll a value in [0.0, 1.0 + LuckBonus]. Select the entry with the
    // highest RollThreshold that is <= the rolled value.
    // Entries with RollThreshold > 1.0 are luck-gated: unreachable at base luck.
    Threshold,
};
```

### Threshold Model

Entries are sorted ascending by `RollThreshold` at asset save time. The roller picks the **highest threshold entry that does not exceed the effective roll value**.

- Gaps between thresholds create dead zones — intentional designer tool, not a bug.
- `LuckBonus` extends the roll ceiling beyond 1.0, making entries above 1.0 reachable.
- There is no guaranteed fallback entry by design. Tables with no entry below the rolled value produce no reward.

---

## Recursion Depth

Maximum nested table depth is **3**. Exceeding this limit at runtime triggers `ensure(false)` in non-shipping builds and silently aborts that branch in shipping. Misconfigured cycles are caught by this guard.

---

## Roll Flow

```
RunLootTable(ULootTable, FLootRollContext, CurrentDepth=0)
  │
  ├─ ensure(CurrentDepth <= 3)  → abort branch if exceeded
  ├─ Resolve RollCount: random int in [RollCount.Min, RollCount.Max]
  │
  └─ For each roll:
       ├─ Generate base value in [0.0, 1.0 + Context.LuckBonus]
       │    └─ If Context.Seed is set: use FRandomStream(FinalSeed + RollIndex)
       ├─ Evaluate FLootTableEntry::EntryRequirements against Context.Instigator
       │    └─ Failed requirements → entry skipped this roll
       ├─ Select highest entry where RollThreshold <= rolled value
       │    └─ No qualifying entry → no reward this roll
       ├─ If entry.NestedTable is set:
       │    └─ RunLootTable(NestedTable, Context, CurrentDepth + 1) → append results
       └─ Else:
            └─ Resolve Quantity in [Quantity.Min, Quantity.Max] using EQuantityDistribution
               Append FLootReward(RewardType, RewardDefinition, Quantity) to results
  │
  └─ Audit: FGameCoreBackend::GetAudit(TAG_Audit_Loot_Roll).RecordEvent(Context, Results)
```

---

## Editor Tooling

All editor tooling lives in the `GameCoreEditor` module. No runtime code is affected.

- **Sort button** — `IDetailCustomization` on `ULootTable` adds a "Sort Entries" button in the Details panel. Sorts `Entries` ascending by `RollThreshold` and marks the asset dirty. One-click fix for designers after adding entries out of order.
- **Validation on save** — `ULootTable::IsDataValid` auto-sorts entries (self-healing, no error emitted for sort order), then checks for duplicate `RollThreshold` values and emits one validation error per duplicate pair. Duplicate thresholds cause non-deterministic entry selection and must be resolved manually.

---

## Sub-pages

- [ULootTable](Loot%20Table%20System/ULootTable.md)
- [FLootTableEntry](Loot%20Table%20System/FLootTableEntry.md)
- [FLootReward](Loot%20Table%20System/FLootReward.md)
- [ILootRewardable](Loot%20Table%20System/ILootRewardable.md)
- [FLootRollContext](Loot%20Table%20System/FLootRollContext.md)
- [ULootRollerSubsystem](Loot%20Table%20System/ULootRollerSubsystem.md)
