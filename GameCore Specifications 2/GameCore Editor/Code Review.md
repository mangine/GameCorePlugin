# GameCore Editor Module — Code Review

---

## Overview

The GameCore Editor module is small and correctly structured: one registration point, one shared utility, zero runtime coupling. There are no significant design flaws. The issues below are minor gaps that should be addressed as the module grows.

---

## Issues

### 1. Stale Type Name in Original Spec (`FLootEntryReward` vs `FLootTableEntry`)

**Severity:** Low | **Status:** Fixed in migration

The original `GameCore Editor.md` registered a `IPropertyTypeCustomization` for `FLootEntryReward::StaticStruct()->GetFName()` and named its customization class `FFLootEntryRewardCustomization`. The Loot Table System spec uses `FLootTableEntry` and `FFLootTableEntryCustomization`. The old name was a stale orphan.

**Fix:** Corrected to `FLootTableEntry` / `FFLootTableEntryCustomization` throughout.

---

### 2. No `GameCoreEditorUtils.cpp` — All Utilities Are Header-Only

**Severity:** Low

Currently fine for a single static inline function, but as utilities grow (e.g. a shared sorted-array button builder, a shared tag container widget), a header-only approach will cause compile-time bloat and complicate forward declarations.

**Suggestion:** When a second utility is added, split into `.h` / `.cpp`. Keep the header for inline predicates only.

---

### 3. `ShutdownModule` Uses `GetModulePtr` but `StartupModule` Uses `LoadModuleChecked`

**Severity:** Low

This asymmetry is actually intentional — `LoadModuleChecked` during startup ensures the `PropertyEditor` module is available, while `GetModulePtr` during shutdown tolerates the module already having been unloaded. This is the correct UE pattern. Worth documenting explicitly so future contributors don't "fix" it.

---

### 4. No Customizations for Other GameCore Systems Yet

**Severity:** Low / Design Note

Several GameCore systems would benefit from editor customizations that don't exist yet:

| System | Potential Customization | Value |
|---|---|---|
| `UFactionRelationshipTable` | Visual matrix view of all faction relationships | High — the flat `ExplicitRelationships` array is hard to read at scale |
| `URequirementList` | Inline requirement descriptions (using `GetDescription()`) in Details | Medium — improves designer iteration |
| `UFactionDefinition` | Rank tag order validation / drag-reorder | Low |

These are suggestions for future iterations, not current gaps.

---

### 5. No Editor Validation Hooks for `GameCoreEditorUtils` Filters

**Severity:** Low

`AssetImplementsInterface` is a picker filter only — it prevents wrong assets from being selected, but it doesn't validate **already-set** asset references that may have become invalid after an interface was removed from a class. `IsDataValid` overrides on the affected data assets (e.g., `UFactionDefinition`, `ULootTable`) are the correct place to catch stale references at cook time.

**Action:** Ensure every data asset that uses interface-filtered pickers has a corresponding `IsDataValid` that verifies the referenced asset still implements the expected interface.
