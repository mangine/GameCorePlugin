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

Entries are sorted ascending by `RollThreshold` at asset save time. The roller picks the **highest threshold entry that does not exceed the rolled value**, then evaluates requirements on that candidate.

- Gaps between thresholds create dead zones — intentional designer tool, not a bug.
- `LuckBonus` extends the roll ceiling beyond 1.0, making entries above 1.0 reachable.
- There is no guaranteed fallback entry by design. Tables with no entry below the rolled value produce no reward.
- If requirements fail on the selected candidate, `bDowngradeOnRequirementFailed` controls whether the roller walks down to the next lower entry or produces no reward.

---

## Recursion Depth

Maximum nested table depth is **3**. Exceeding this limit at runtime triggers `ensure(false)` in non-shipping builds and silently aborts that branch in shipping. Misconfigured cycles are caught by this guard.

---

## Roll Flow

```
RunLootTable(ULootTable, FLootRollContext)
  │
  ├─ Seed setup (once, before any rolls):
  │    If Context.Seed != INDEX_NONE:
  │      RandomOffset = FMath::RandRange(0, MAX_int32)
  │      FinalSeed    = HashCombine(Context.Seed, RandomOffset)
  │      Stream       = FRandomStream(FinalSeed)   ← single stream, advanced throughout
  │
  └─ RollTableInternal(Table, Context, Depth=0, Stream)
       │
       ├─ Guard: Depth > 3 → ensure(false), return {}
       ├─ Resolve RollCount: random int in [RollCount.Min, RollCount.Max]
       │
       └─ For each roll:
            ├─ RolledValue = Stream ? Stream->FRandRange(0, 1+LuckBonus)
            │                       : FMath::FRandRange(0, 1+LuckBonus)
            │
            ├─ Find candidate: highest entry where RollThreshold <= RolledValue
            │    └─ No qualifying entry → no reward this roll
            │
            ├─ Build FRequirementContext from Context.Instigator->GetPawn()
            │
            ├─ Downgrade loop on candidate:
            │    ├─ EvaluateAll(EntryRequirements) passes → use this entry
            │    ├─ Fails + bDowngradeOnRequirementFailed == true  → move to next lower entry
            │    └─ Fails + bDowngradeOnRequirementFailed == false → no reward this roll
            │
            ├─ If entry.NestedTable is set:
            │    └─ RollTableInternal(NestedTable, Context, Depth+1, Stream) → append results
            └─ Else:
                 ├─ Quantity = ResolveQuantity(Entry.Quantity, Entry.QuantityDistribution, Stream)
                 └─ Append FLootReward{RewardType, RewardDefinition, Quantity}
  │
  └─ Audit: FGameCoreBackend::GetAudit(TAG_GameCore_Audit_Loot_Roll).RecordEvent(Context, Results)
```

---

## Editor Tooling

All editor tooling lives in the `GameCoreEditor` module. No runtime code is affected.

- **Sort button** — `IDetailCustomization` on `ULootTable` adds a "Sort Entries" button in the Details panel. Sorts `Entries` ascending by `RollThreshold` and marks the asset dirty.
- **Validation on save** — `ULootTable::IsDataValid` auto-sorts entries (self-healing, no error emitted for sort order), then checks for duplicate `RollThreshold` values and emits one validation error per duplicate pair.

---

## Sub-pages

- [ULootTable](Loot%20Table%20System/ULootTable.md)
- [FLootTableEntry](Loot%20Table%20System/FLootTableEntry.md) — also contains `FLootEntryReward` and `EQuantityDistribution`
- [FLootReward](Loot%20Table%20System/FLootReward.md)
- [ILootRewardable](Loot%20Table%20System/ILootRewardable.md)
- [FLootRollContext](Loot%20Table%20System/FLootRollContext.md)
- [ULootRollerSubsystem](Loot%20Table%20System/ULootRollerSubsystem.md)
