# Loot Table System — Architecture

Server-side only. Produces `TArray<FLootReward>` from a `ULootTable` data asset given a roll context. Callers decide how to fulfill rewards — the loot system never touches inventory, currency, or progression directly.

---

## Dependencies

### Runtime Module Dependencies
| Module | Reason |
|---|---|
| `GameCore` | All runtime classes live here |
| `GameplayTags` | `FGameplayTag` on `FLootRollContext`, `FLootEntryReward`, `FLootReward` |
| `GameplayAbilities` | `Attribute.Luck` read from instigator's ASC in `ResolveLuckBonus` |

### Editor-Only Module Dependencies
| Module | Reason |
|---|---|
| `GameCoreEditor` | `FFLootEntryRewardCustomization`, `FULootTableCustomization`, sort button |
| `UnrealEd` | `IDetailCustomization`, `IPropertyTypeCustomization` |
| `PropertyEditor` | `SObjectPropertyEntryBox`, `IDetailLayoutBuilder` |

### GameCore Plugin System Dependencies
| System | Usage |
|---|---|
| **Requirement System** | `URequirement`, `URequirementLibrary::EvaluateAll`, `FRequirementContext` — per-entry gates |
| **Backend / Audit** | `FGameCoreBackend::GetAudit(TAG_GameCore_Audit_Loot_Roll).RecordEvent(...)` — every roll, always |
| **GameCore Editor** | `GameCoreEditorUtils::AssetImplementsInterface` — `ILootRewardable` picker filter |

> The loot system has **no dependency** on the Inventory, Currency, Progression, or Serialization systems. It is a pure producer — fulfillment is the caller's responsibility.

---

## Requirements

- Every loot table entry has a configurable roll threshold.
- A loot table entry can contain a nested loot table, recursively rolled when selected.
- Each entry defines a min/max quantity range and a distribution curve.
- The roller supports a variable roll count per table invocation.
- Luck modifiers extend the roll ceiling beyond 1.0, making previously unreachable entries accessible.
- Roll ceiling extension is via `LuckBonus` — table data is stable regardless of player modifiers.
- Per-entry requirement gates condition individual entries on player state.
- On requirement failure the designer chooses between losing the reward or downgrading to the next lower entry.
- Every roll is audited via the Backend audit channel regardless of result.
- Historical rolls are exactly reproducible from a stored seed.
- No forced dependency on inventory, currency, or progression.

---

## Features

- **Threshold selection model** — bracket-based, luck-gated entries above threshold 1.0, intentional dead zones via gaps.
- **Recursive nested tables** — up to depth 3; cycles are caught by depth guard.
- **Per-entry requirement gates** — evaluated after candidate selection; downgrade walk on failure if configured.
- **Quantity distribution** — `Uniform` and `Normal` (triangular approximation) per entry.
- **Deterministic seeded rolling** — single advancing `FRandomStream` from a derived `FinalSeed`; full sequence reproducible from one seed.
- **Stateful luck modifiers** — `RegisterModifier`/`UnregisterModifier` API on the subsystem; GAS Luck attribute summed automatically.
- **Always-fire audit** — every call to `RunLootTable` emits to the audit channel, including empty results.
- **Asset Manager integration** — `ULootTable` as `UPrimaryDataAsset`, registered by type for async loading.
- **Editor tooling** — sort button, `IsDataValid` auto-sort + duplicate threshold detection, `ILootRewardable`-filtered asset picker.

---

## Design Decisions

**Threshold model over weighted random** 
Threshold naturally supports luck-gated entries (threshold > 1.0 = unreachable at base luck). Weighted random cannot express "unreachable until lucky enough" cleanly. Gaps are intentional dead zones — a designer tool for controlling the probability of no reward.

**`ELootTableType` enum with only `Threshold` implemented** 
Leaves the door open for weighted random or sequential models without breaking the API. Adding a new type requires one enum value and one new code path in `RollTableInternal`.

**Requirements evaluated after candidate selection, not during scan** 
Evaluating during scan produces confusing fallthrough: a requirement failure on an intermediate entry silently exposes the next lower entry. The correct model is "this threshold won, now check eligibility". Downgrade is explicit per-entry (`bDowngradeOnRequirementFailed`).

**`bDowngradeOnRequirementFailed` is per-entry** 
Different entries in the same table have different intent — a rare drop that should disappear vs. a common drop that should gracefully degrade. Per-entry gives designers precise control without separate tables.

**`ILootRewardable` marker interface over shared base class** 
Any class can implement an interface regardless of its inheritance chain. A shared base class (`ULootRewardDefinition`) would force hierarchy restructuring for any asset type that already inherits a required base. The interface is empty — its presence is the sole contract.

**`FLootEntryReward` separated from `FLootReward`** 
Authoring and output have different roles: `FLootEntryReward` is `EditAnywhere` in data assets; `FLootReward` is the read-only output returned to callers. Collapsing them into one struct creates ambiguity about which fields are valid when.

**Single advancing `FRandomStream` for determinism** 
A single stream created before any rolls, threaded through all recursive calls and quantity resolution. Ensures the full roll sequence across a table invocation — including nested tables — is reproducible from one `FinalSeed`. Per-roll streams (new stream per roll index) produce nearly identical values at similar indices, breaking statistical distribution.

**`INDEX_NONE` as unseeded sentinel instead of `TOptional<int32>`** 
`TOptional<T>` is not supported as a `UPROPERTY` — UHT cannot process it. `INDEX_NONE` follows UE convention. Seed value -1 cannot be explicitly requested; always treated as unseeded. CS tooling generates seeds in positive integer space.

**`ULootRollerSubsystem` as `UGameInstanceSubsystem`** 
Luck modifiers are stateful — buff systems and event managers register modifiers that persist across roll calls. A function library has no home for that state. The subsystem owns modifier registration and is the single roll authority.

**No guaranteed fallback entry** 
Tables with no entries below the rolled value produce nothing. Designers who want a guaranteed reward create an entry at threshold 0.0 explicitly. Implicit guarantees hide designer intent.

---

## Explicit Non-Features

- **Guaranteed fallback entry** — use threshold 0.0 explicitly.
- **Group loot distribution** (round-robin, need/greed) — fulfillment-layer concern. `FLootRollContext::GroupActor` provides group identity for routing.
- **`MaxLuckBonus` cap in the loot system** — GAS `Attribute.Luck` and buff system are the authoritative ceiling.
- **Weighted random selection** — not implemented; `ELootTableType` has a slot for it.
- **Async rolling** — tables are expected to be small and already loaded. Sync load of nested table soft references is a cache hit.
- **Client-side rolling** — server-authoritative only. `HasAuthority()` guard at the top of `RunLootTable`.
- **Per-table requirement gates** — requirements exist per-entry only. The caller checks table-level requirements before calling `RunLootTable`.

---

## Logic Flow

```
Caller
  │
  ├─ ULootRollerSubsystem::ResolveLuckBonus(Instigator, SourceTag)
  │    ├─ Sum registered FLootModifier entries matching SourceTag (hierarchy match)
  │    ├─ Add GAS Attribute.Luck from Instigator->GetPawn()->ASC (if present)
  │    └─ Clamp to >= 0.0, return
  │
  ├─ Build FLootRollContext { Instigator, SourceTag, LuckBonus, Seed }
  │
  └─ ULootRollerSubsystem::RunLootTable(ULootTable*, FLootRollContext)
       │
       ├─ Guard: HasAuthority() — return {} on client
       ├─ Guard: Table != nullptr — ensure() + return {}
       │
       ├─ Seed setup (once, top-level only):
       │    If Context.Seed != INDEX_NONE:
       │      RandomOffset = FMath::RandRange(0, MAX_int32)
       │      FinalSeed    = HashCombine(Context.Seed, RandomOffset)
       │      Stream       = FRandomStream(FinalSeed)
       │
       └─ RollTableInternal(Table, Context, Depth=0, Stream)
            │
            ├─ Guard: Depth > MaxRecursionDepth (3) → ensure(false), return {}
            ├─ Resolve RollCount from FInt32Range via Stream or FMath
            │
            └─ For each roll:
                 ├─ RolledValue = FRandRange(0, 1 + LuckBonus) via Stream or FMath
                 ├─ Scan Table.Entries descending → highest RollThreshold <= RolledValue
                 ├─ No candidate → no reward this roll
                 │
                 ├─ Build FRequirementContext::ForActor(Instigator->GetPawn())
                 │
                 ├─ Downgrade loop:
                 │    ├─ URequirementLibrary::EvaluateAll(EntryRequirements, ReqContext) passes → use entry
                 │    ├─ Fails + bDowngradeOnRequirementFailed → CandidateIndex -= 1, retry
                 │    └─ Fails + !bDowngradeOnRequirementFailed → no reward this roll
                 │
                 ├─ If SelectedEntry.NestedTable is set:
                 │    └─ RollTableInternal(NestedTable, Context, Depth+1, Stream) → append
                 └─ Else:
                      ├─ ResolveQuantity(Quantity, QuantityDistribution, Stream)
                      └─ Append FLootReward{RewardType, RewardDefinition, Quantity}
            │
            └─ Audit:
                 FGameCoreBackend::GetAudit(TAG_GameCore_Audit_Loot_Roll)
                   .RecordEvent(Instigator, SourceTag, Table, FinalSeed, LuckBonus, Results)
```

---

## Known Issues

### Synchronous nested table load
`FLootTableEntry::NestedTable` is a `TSoftObjectPtr<ULootTable>` loaded via `LoadSynchronous()` during a roll. The spec notes this is safe when tables are already in memory (actors in the loaded level have their assets resident). However, if a nested table asset is not already loaded this will stall the game thread. There is no pre-validation that nested table assets are loaded before rolling begins.

### `IsDataValid` mutates `const` object
`ULootTable::IsDataValid` is `const` but calls `const_cast<ULootTable*>(this)->Entries.Sort(...)` to auto-sort. This is a design smell — a validation method should not mutate state. The sort belongs in an explicit `PostEditChangeProperty` or the sort button callback, not in the validation path.

### Downgrade walk does not skip nested-table entries
If an entry with `NestedTable` set fails requirements and `bDowngradeOnRequirementFailed` is true, the walk decrements to the next lower entry. The next lower entry might also be a nested table entry. The spec does not define behavior for a downgrade chain that lands on a nested table entry — this should be explicitly handled.

### Seed reproducibility requires identical asset state
Reproducing a roll from `FinalSeed` requires the exact same `ULootTable` asset as the original roll. Adding or removing entries changes the stream sequence. The audit captures `TableAsset` and `FinalSeed` for this purpose, but there is no version stamp on the table asset to detect drift.

### `FLootModifierHandle` ID generation not specified
The spec shows the handle struct but does not specify how `Id` is generated (e.g. atomic counter). The implementation must define this.

---

## File Structure

```
GameCore/
  Source/
    GameCore/
      Public/
        LootTable/
          ILootRewardable.h
          FLootRollContext.h
          FLootReward.h
          FLootTableEntry.h        ← also contains FLootEntryReward, EQuantityDistribution, ELootTableType
          ULootTable.h
          ULootRollerSubsystem.h
      Private/
        LootTable/
          ULootTable.cpp
          ULootRollerSubsystem.cpp
    GameCoreEditor/
      Public/
        LootTable/
          FULootTableCustomization.h
          FFLootEntryRewardCustomization.h
      Private/
        LootTable/
          FULootTableCustomization.cpp
          FFLootEntryRewardCustomization.cpp
```

### Asset Manager Registration (`DefaultGame.ini`)
```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(PrimaryAssetType="LootTable",AssetBaseClass=/Script/GameCore.LootTable,bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/LootTables")),Rules=(Priority=0,bApplyRecursively=True))
```
Adjust `Directories` to match the project content layout. The type name `"LootTable"` must match `ULootTable::GetPrimaryAssetId()` implementation.
