# GameCoreEditor — Implementation Deviations

## 1. Sort Direction: Ascending, Not Descending

**Spec location:** Task description said "sorts `ULootTable::Entries` by `RollThreshold` descending".

**Authoritative spec:** `ULootTable.md` (Editor Tooling section) states the button "Sorts `Entries` ascending by `RollThreshold` in-place", consistent with `IsDataValid`'s auto-sort which also sorts ascending. The roller scans entries in descending order at runtime to find the highest qualifying candidate — so the *stored* order must be ascending.

**Decision:** `LootTableCustomization.cpp` sorts ascending. This matches `ULootTable::IsDataValid` behavior and the roller's assumption.

---

## 2. `FFLootTableEntryCustomization` Performs Recursive Child Traversal

**Spec location:** `FFLootEntryRewardCustomization.md` shows flat iteration over `FLootEntryReward` children (the struct that directly owns `RewardDefinition`).

**Registered type:** `GameCoreEditor.cpp` registers the customization against `FLootTableEntry` (the outer struct), not `FLootEntryReward`. `FLootTableEntry` embeds `FLootEntryReward` as a nested field (`Reward`).

**Decision:** `FFLootTableEntryCustomization::ProcessChildren` is written as a recursive helper that walks into nested struct children. This ensures the `GameCoreInterfaceFilter` meta tag on `FLootEntryReward::RewardDefinition` is found even though the customization is registered at the `FLootTableEntry` level. If the codebase later registers a separate customization directly on `FLootEntryReward`, the recursive walk in `FLootTableEntry`'s customization should be replaced with a flat one-level scan to avoid double-customizing the nested struct.

---

## 3. `GetAssetsImplementingInterface` Added to `GameCoreEditorUtils`

**Spec location:** `GameCoreEditorUtils.md` only declares `AssetImplementsInterface`. The task description additionally requested `GetAssetsImplementingInterface`.

**Decision:** Both functions are included in `GameCoreEditorUtils.h`. `GetAssetsImplementingInterface` performs a full Asset Registry scan and does not load assets. It is unused internally (no current consumer calls it directly from within this module), but is provided as a public utility per the task description.

---

## 4. `SortEntries` Uses `Modify()` + `MarkPackageDirty()` Instead of Property Handle API

**Spec location:** `ULootTable.md` says the button "Marks the asset dirty". No implementation detail is prescribed beyond that.

**Decision:** The implementation calls `LootTable->Modify()` (records state for undo via the transaction system) and `LootTable->MarkPackageDirty()` (marks the package as needing a save). This is the standard UE pattern for undoable in-memory mutations on `UObject` assets. The `FScopedTransaction` wrapper ensures the operation appears in the Ctrl+Z history.

---

## 5. `ANY_PACKAGE` Interface Resolution

**Spec location:** `FFLootEntryRewardCustomization.md` uses `FindObject<UClass>(ANY_PACKAGE, ...)` to resolve the interface class from the meta string.

**Note:** `ANY_PACKAGE` is deprecated in UE5 in favour of searching with a `nullptr` outer or using `StaticFindFirstObject`. Since the spec explicitly uses `ANY_PACKAGE` and the `GameCoreEditor.Build.cs` targets UE5.7, and `ANY_PACKAGE` still compiles (with a deprecation warning) in UE5.7, it has been preserved to match the spec. If the deprecation warning is unacceptable, replace with `StaticFindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *InterfaceName))`.
