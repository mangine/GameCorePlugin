# GameCore Editor Module — Architecture

**Module:** `GameCoreEditor` | **Type:** Editor-only | **UE Version:** 5.7

The `GameCoreEditor` module provides all editor customizations, property type overrides, and shared editor utilities for the GameCore plugin. It is **never compiled into shipping builds**.

---

## Design Principles

- **Zero runtime coupling.** Nothing in this module is reachable at runtime. All classes either live in the editor module or are guarded by `#if WITH_EDITOR`.
- **Shared utilities over duplication.** Common patterns (interface-filtered asset pickers, etc.) live in `GameCoreEditorUtils` and are reused across all customizations.
- **Registered at module startup / unregistered at shutdown.** All `IDetailCustomization` and `IPropertyTypeCustomization` entries are registered in `FGameCoreEditorModule::StartupModule` and unregistered in `ShutdownModule`. No registration is scattered across individual customization files.

---

## Dependencies

### Unreal Engine Modules

| Module | Use |
|---|---|
| `UnrealEd` | `IDetailCustomization`, `IPropertyTypeCustomization` base classes |
| `PropertyEditor` | `FPropertyEditorModule` — registration target |
| `AssetRegistry` | `FAssetData::GetClass()` — used in asset picker filters without loading assets |
| `Slate` / `SlateCore` | Widget construction in customizations |
| `EditorWidgets` | `SObjectPropertyEntryBox` |

### GameCore Runtime Modules Referenced

Editor customizations reference runtime types for registration keys. These are **read-only references** (class names, struct names) — no runtime logic is invoked.

| Type | Module | Used For |
|---|---|---|
| `ULootTable` | `GameCore` (Loot Table System) | `RegisterCustomClassLayout` key |
| `FLootTableEntry` | `GameCore` (Loot Table System) | `RegisterCustomPropertyTypeLayout` key |
| `ILootRewardable` | `GameCore` (Loot Table System) | Asset picker interface filter |

---

## Module Registration

**File:** `GameCoreEditor/GameCoreEditorModule.h / .cpp`

```cpp
void FGameCoreEditorModule::StartupModule()
{
    FPropertyEditorModule& PropertyModule =
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    // ULootTable — "Sort Entries" button in Details panel
    PropertyModule.RegisterCustomClassLayout(
        ULootTable::StaticClass()->GetFName(),
        FOnGetDetailCustomizationInstance::CreateStatic(
            &FULootTableCustomization::MakeInstance));

    // FLootTableEntry — ILootRewardable-filtered asset picker on RewardDefinition
    PropertyModule.RegisterCustomPropertyTypeLayout(
        FLootTableEntry::StaticStruct()->GetFName(),
        FOnGetPropertyTypeCustomizationInstance::CreateStatic(
            &FFLootTableEntryCustomization::MakeInstance));
}

void FGameCoreEditorModule::ShutdownModule()
{
    if (FPropertyEditorModule* PropertyModule =
        FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
    {
        PropertyModule->UnregisterCustomClassLayout(
            ULootTable::StaticClass()->GetFName());
        PropertyModule->UnregisterCustomPropertyTypeLayout(
            FLootTableEntry::StaticStruct()->GetFName());
    }
}
```

> **Note:** The class and struct names above (`FLootEntryReward` in the old spec) have been corrected to `FLootTableEntry` to match the actual Loot Table System types. Update if the Loot Table System spec uses a different struct name.

---

## Registered Customizations

| Target Type | Customization Class | What It Adds |
|---|---|---|
| `ULootTable` | `FULootTableCustomization` | "Sort Entries" button in the Details panel |
| `FLootTableEntry` | `FFLootTableEntryCustomization` | `ILootRewardable`-filtered asset picker on `RewardDefinition` |

New customizations must be registered here. **Do not self-register** inside individual customization constructors.

---

## File Structure

```
GameCore/Source/GameCoreEditor/
├── GameCoreEditorModule.h / .cpp         ← FGameCoreEditorModule (IModuleInterface)
└── Utils/
    └── GameCoreEditorUtils.h             ← Shared editor utility functions (header-only)
```

Customizations added by other systems live in their system's editor subfolder and are registered centrally here:

```
GameCore/Source/GameCoreEditor/
└── LootTable/
    ├── LootTableCustomization.h / .cpp       ← FULootTableCustomization
    └── LootTableEntryCustomization.h / .cpp  ← FFLootTableEntryCustomization
```

---

## Adding a New Customization — Checklist

```
1. Create the customization class under the relevant system subfolder in GameCoreEditor/.
2. Implement IDetailCustomization or IPropertyTypeCustomization.
3. Use GameCoreEditorUtils helpers where applicable.
4. Register in FGameCoreEditorModule::StartupModule.
5. Unregister in FGameCoreEditorModule::ShutdownModule.
6. Update the Registered Customizations table above.
```
