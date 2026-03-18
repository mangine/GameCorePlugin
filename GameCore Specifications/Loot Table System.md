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

## Design Decisions & Requirements

This section records the feature requirements the system was designed to satisfy, the key design decisions made during specification, and the explicit boundaries of the system. It exists so that future readers can understand *why* the system works the way it does, not just *what* it does.

---

### Feature Requirements

These are the capabilities the system was required to support:

- Every loot table entry has a configurable roll chance expressed as a threshold value.
- A loot table entry can contain a nested loot table, which is recursively rolled when that entry is selected.
- Each entry defines a min/max quantity range for the reward it grants.
- The roller must support a variable number of rolls per table invocation (roll count range).
- Luck modifiers must be able to make previously unreachable entries reachable — entries above threshold 1.0 are intentionally inaccessible at base luck and become reachable as luck increases.
- The rolled value ceiling is extended by `LuckBonus`, not the entry thresholds themselves. This keeps table data stable regardless of player modifiers.
- The system must support per-entry requirement gates so individual entries can be conditioned on player state (level, quest progress, traits, etc.).
- When a requirement gate fails, the designer must be able to choose between losing the reward entirely or downgrading to the next lower entry.
- The roller must be callable multiple times on the same table with independent results.
- Every roll must be auditable for live-ops economy tracking and CS support.
- Historical rolls must be exactly reproducible from a stored seed for CS investigation.
- The system must not force any dependency on inventory, currency, or progression — fulfillment is always the caller's responsibility.

---

### Key Design Decisions

**Threshold model over weighted random**

Two selection models were considered: threshold (bracket) selection and weighted random selection. Threshold was chosen because it naturally supports the core requirement of luck-gated entries — entries with thresholds above 1.0 are simply unreachable until `LuckBonus` extends the roll ceiling past their threshold. In a weighted random model, every entry always has nonzero probability, making the concept of "unreachable until lucky enough" impossible to express cleanly. The tradeoff is that gaps between thresholds create dead zones where a roll produces no reward — this is intentional designer behaviour, not a defect. Designers use gaps to control the probability of getting nothing.

**`ELootTableType` enum with only `Threshold` implemented**

The enum exists to leave the door open for future selection models (e.g. weighted random, sequential) without requiring a breaking API change. Only `Threshold` is implemented now. Adding a new type later requires adding an enum value and a new code path in `RollTableInternal` — no struct or interface changes.

**Requirements evaluated after candidate selection, not during scan**

An earlier design evaluated requirements during the ascending entry scan, skipping entries that failed and moving to the next. This was rejected because it produces confusing designer behaviour: a requirement failure on an intermediate entry silently exposes the next lower entry as the winner, which is not what designers expect. The correct mental model is: "this threshold won, now check if the player is eligible." Requirements are therefore evaluated on the winning candidate only, with an explicit downgrade decision (`bDowngradeOnRequirementFailed`) rather than silent fallthrough.

**`bDowngradeOnRequirementFailed` is per-entry, not per-table**

Making downgrade a per-table flag was considered but rejected. Different entries in the same table may have very different intent: a rare drop that should silently disappear on failure vs. a common drop that should gracefully degrade. Per-entry control gives designers precise intent without needing separate tables.

**`ILootRewardable` interface over base class inheritance for reward asset filtering**

The reward asset reference on `FLootEntryReward` needed to be constrained so designers can only slot compatible assets. Three approaches were evaluated:

- *Inheriting from `FPrimaryAssetId`*: rejected — `FPrimaryAssetId` is a plain struct with no UE reflection support. Subclassing it provides no editor filtering benefit.
- *Shared base class (`ULootRewardDefinition`)*: rejected — UObject single inheritance means any asset type that already has its own required base class (e.g. `UItemDefinition` inheriting from a game-specific base) cannot also inherit from `ULootRewardDefinition` without restructuring its entire hierarchy. This forces developers to design their asset classes around loot compatibility, which is an unacceptable constraint for a generic plugin.
- *Marker interface (`ILootRewardable`)*: chosen — any class can implement an interface regardless of its inheritance chain. `UItemDefinition : public UPrimaryDataAsset, public ILootRewardable` is a one-line addition with zero hierarchy disruption. The interface is empty (marker only); its presence is the sole contract.

The editor asset picker for `FLootEntryReward::RewardDefinition` is filtered via `IPropertyTypeCustomization` (`FFLootEntryRewardCustomization`) using `GameCoreEditorUtils::AssetImplementsInterface`. This is an editor-only authoring constraint — it is not enforced at runtime. The `TSoftObjectPtr<UObject>` type is used at the C++ level precisely because the constraint is interface-based, not type-based. The fulfillment layer casts to the expected type after async load.

The `IPropertyTypeCustomization` infrastructure is reusable: any future struct property that needs interface-filtered picking adds `meta = (GameCoreInterfaceFilter = "InterfaceName")` and registers its own customization using the same `CustomizeChildren` pattern. `GameCoreEditorUtils::AssetImplementsInterface` needs no changes.

**`FLootEntryReward` separated from `FLootReward`**

`FLootReward` serves two conceptually different roles: authoring (designer fills in `RewardType` and `RewardDefinition` in the asset) and output (the roller returns resolved rewards to the fulfillment layer). Collapsing these into one struct creates confusion — `EditAnywhere` on an output-only struct, or `Quantity` being 0 in the authored context but always ≥ 1 on output. The split into `FLootEntryReward` (authoring) and `FLootReward` (output) makes both roles unambiguous and prevents incorrect use at authoring time.

**Single advancing `FRandomStream` for determinism**

When seeded, a single `FRandomStream` is created at the start of `RunLootTable` and threaded through all recursive calls and into `ResolveQuantity`. All rolls — `RollCount`, entry value rolls, quantity rolls, and nested table rolls — advance the same stream sequentially. This ensures the entire roll sequence across a table invocation is reproducible from one `FinalSeed`. A per-roll stream (creating a new `FRandomStream(Seed + RollIndex)` per roll) was rejected because rolls at similar indices produce nearly identical values, breaking statistical distribution. The `FinalSeed` is derived by hashing the caller-provided seed with a random offset, ensuring the raw seed value is not directly predictable. Important: reproducing a roll requires the exact same `ULootTable` asset state as the original roll, since adding or removing entries changes the stream sequence. The audit payload captures both `FinalSeed` and `TableAsset` for this reason.

**`INDEX_NONE` sentinel for seed instead of `TOptional<int32>`**

`TOptional<T>` is not supported as a `UPROPERTY` by Unreal's reflection system — UHT cannot process it. Using it in a `USTRUCT` with `UPROPERTY` would fail to compile. `INDEX_NONE` (-1) is used as the "unseeded" sentinel instead, following UE convention. The practical limitation is that seed value -1 cannot be explicitly requested; `INDEX_NONE` is always treated as unseeded. This is documented and acceptable — CS tooling generates seeds in the positive integer space.

**`ULootRollerSubsystem` as a `UGameInstanceSubsystem`**

A function library was considered for the roller. A subsystem was chosen because luck modifiers are stateful — buff systems and event managers register modifiers that persist across multiple roll calls. A library function has no home for that state. The subsystem owns modifier registration and is the single roll authority. This also makes the subsystem mockable and testable in isolation.

**No guaranteed fallback entry**

No mechanism forces a table to always produce a reward. Tables with no entries below the rolled value produce nothing. This is intentional: designers who want a guaranteed reward create an entry at threshold 0.0 explicitly. Implicit guarantees would hide designer intent and make it impossible to express "this table sometimes produces nothing."

---

### Explicit Non-Features

These were considered and consciously excluded. Do not add them without a deliberate design discussion.

- **Guaranteed fallback entry** — no automatic "always drop something" mechanism. Use threshold 0.0 explicitly if a guaranteed reward is needed.
- **Group loot distribution** (round-robin, need/greed, master looter) — this is a fulfillment-layer concern. `FLootRollContext::GroupActor` provides the group identity for the fulfillment layer to route correctly. The loot system produces a flat reward list; who gets what is not its responsibility.
- **`MaxLuckBonus` cap in the loot system** — the GAS `Attribute.Luck` and buff system design are the authoritative ceiling for luck. Capping it again in the loot system would be redundant and could silently contradict designer intent set elsewhere.
- **Weighted random selection** — not implemented. `ELootTableType` has a slot for it if needed in the future. See the threshold vs weighted random decision above for why threshold was preferred.
- **Async rolling** — the roller is synchronous. Tables are expected to be small and already loaded (attached to mobs/chests that are in memory). Async rolling would add significant complexity with minimal benefit given the usage context.
- **Client-side rolling** — all rolling is server-authoritative. Clients never call `RunLootTable`. Predicted loot (showing a reward before server confirmation) is a UI concern handled outside this system.
- **Per-table requirement gates** — requirements exist per-entry only. A table-level gate (skip the entire table if requirements fail) can be achieved by the caller checking requirements before calling `RunLootTable`.
