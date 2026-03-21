# Loot Table System — Code Review

---

## Overview

The Loot Table System is well-architected for its stated scope. The threshold selection model is correct for luck-gated design, the output/authoring struct split is clean, the audit integration is sound, and the marker interface approach for `ILootRewardable` is the right call. The system is correctly decoupled — it produces rewards but fulfills nothing.

The issues below range from design smells to genuine correctness risks.

---

## Issues

### 1. `IsDataValid` Mutates a `const` Object (Design Smell)

`ULootTable::IsDataValid` is declared `const` (required by the UE API) but sorts `Entries` via `const_cast`. This is semantically wrong — a validation method should not have side effects. If the sort fails for any reason (e.g. a corrupted array), the mutation happens inside a path that the caller does not expect to be mutating.

**Fix:** Move the auto-sort to `PostEditChangeProperty` and `PostLoad`. `IsDataValid` should read-only validate. If sorting in `IsDataValid` is kept as a pragmatic compromise, document explicitly that it is a cook-time self-heal and not a general contract.

---

### 2. Synchronous Nested Table Load Is a Hidden Stall Risk

`SelectedEntry.NestedTable.LoadSynchronous()` inside `RollTableInternal` will block the game thread if the nested table asset is not resident. The spec states "tables are expected to be already loaded" but there is no enforcement: no `ensure(NestedTable.IsValid())` before the sync load, no pre-load step, no cook-time validation that nested table references are in the same asset bundle as their parent.

With 3 levels of nesting this can compound to 3 synchronous disk reads in a single `RunLootTable` call.

**Fix:** Add an `ensure(NestedTable.Get() != nullptr)` before the sync load and emit a warning. For production: validate at cook time that nested table assets are bundled with their parent actor's primary asset bundle.

---

### 3. Downgrade Walk Behavior on Nested Table Entries Is Undefined

If an entry with `NestedTable` set fails requirements and `bDowngradeOnRequirementFailed` is true, the walk decrements to `CandidateIndex - 1`. That entry may also have a `NestedTable`. The spec does not define what this means — should the nested table be rolled? Should nested table entries be skipped during downgrade? This ambiguity will produce inconsistent designer expectations.

**Fix:** Explicitly document whether nested table entries participate in downgrade chains. A clean rule: nested table entries that fail requirements are skipped during downgrade (downgrade only applies to leaf reward entries). The implementation should check `SelectedEntry.NestedTable.IsNull()` before allowing a downgrade candidate.

---

### 4. `FLootRollContext::Instigator` Is Not a `UPROPERTY`

The spec correctly notes that `TWeakObjectPtr` is not safe as a `UPROPERTY`. This is true. However, the consequence is that `FLootRollContext` cannot be used as a Blueprint-accessible parameter struct for the full context — only `SourceTag`, `LuckBonus`, and `Seed` are Blueprint-visible. This creates a discrepancy where the struct is `BlueprintType` but partially unusable from Blueprint.

Since `RunLootTable` is intentionally C++ server-side only with no `BlueprintCallable` exposure, this is acceptable. But `FLootRollContext` should not be `BlueprintType` if it cannot be fully constructed from Blueprint. Consider removing `BlueprintType` from the struct or splitting into a BP-constructable partial struct and a C++ internal context.

---

### 5. `FLootModifierHandle` ID Generation Is Not Thread-Safe

`NextHandleId` is a plain `uint32`. If `RegisterModifier` is ever called from multiple threads (e.g. event systems that register modifiers during async callbacks), the increment is a data race. The subsystem is a `UGameInstanceSubsystem` so calls should be on the game thread, but this assumption is not asserted anywhere.

**Fix:** Add `check(IsInGameThread())` to `RegisterModifier` and `UnregisterModifier`. This documents the threading contract and catches violations in non-shipping builds.

---

### 6. No Validation That `EntryRequirements` Are Synchronous

The spec states "must contain only synchronous requirements — validated at cook time" but no cook-time validation is specified. `URequirement` subclasses that are async would silently misbehave inside `RollTableInternal` (which never suspends).

**Fix:** Add a cook-time or `IsDataValid` check: iterate `EntryRequirements` and verify each requirement's declared evaluation mode is synchronous. This requires `URequirement` to expose an `ERequirementEvaluationMode` or similar. If that capability does not exist on `URequirement` yet, document this as a known gap.

---

### 7. No Version Stamp on `ULootTable` for Seed Reproducibility

Reproducing a seeded roll requires the exact same `ULootTable` asset as the original. Adding or removing entries silently changes the stream sequence. The audit records `TableAsset` (soft path) but not an asset version, CL, or content hash. A CS investigation months later on a patched table will silently produce different results.

**Suggestion:** Add an `int32 AssetVersion` (or `FGuid`) `UPROPERTY` to `ULootTable` that designers increment when they make structural changes to entries. Record this in the audit payload alongside `FinalSeed`. This allows CS tools to detect version drift when attempting reproduction.

---

### 8. `RollCount` Is an `FInt32Range` With No Minimum-of-Zero Guard

`FInt32Range` allows min = 0 (or negative). A `RollCount` of `FInt32Range(0, 0)` is a valid asset but produces no output on every call, silently. A designer creating this accidentally has no feedback.

**Fix:** Add a validation in `IsDataValid`: `if (RollCount.GetLowerBoundValue() < 1)` → emit a warning. A `RollCount` that can produce zero rolls is almost always a configuration mistake.

---

### 9. `ELootTableType` Is Unused in `RollTableInternal`

The spec mentions `TableType` on `ULootTable` but `RollTableInternal` does not check it — Threshold logic runs unconditionally. If a second `ELootTableType` value is added later without adding the corresponding code path, it will silently fall through to Threshold behavior. This is a latent future bug.

**Fix:** Add a `switch (Table->TableType)` in `RollTableInternal` with a `default: ensure(false); return {};` arm. This makes the missing implementation explicit rather than silently wrong.

---

## Suggestions (Non-Issues)

**Pre-roll asset validation helper.** Expose a `ValidateTable(const ULootTable*)` method on the subsystem that checks all nested table soft references are loaded and all entry requirements are synchronous. Call this in development before rolling in systems tests.

**Batch rolling API.** A common caller pattern is rolling the same table for multiple players simultaneously (boss kill with a group). Adding `RunLootTableForGroup(Table, TArray<FLootRollContext>)` that calls `RollTableInternal` once per context reuses the sorted entry array without repeated setup overhead — minor but clean.

**Quantity minimum guard.** `ResolveQuantity` can technically return 0 if `FInt32Range(0, 0)` is authored. Since `FLootReward::Quantity` documents "Always >= 1", add `return FMath::Max(1, result)` at the end of `ResolveQuantity`.
