# GameCore Editor Module

Editor-only module (`GameCoreEditor`). Never compiled into shipping builds. Contains all `IDetailCustomization`, `IPropertyTypeCustomization`, and shared editor utilities for the GameCore plugin.

---

## Design Principles

- **Zero runtime coupling.** No code in this module is reachable at runtime. All classes are guarded by `#if WITH_EDITOR` or live exclusively in the editor module.
- **Shared utilities over duplication.** Common editor patterns (interface-filtered asset pickers, sorted array buttons) live in `GameCoreEditorUtils` and are reused across all customizations.
- **Registered at module startup.** All `IDetailCustomization` and `IPropertyTypeCustomization` registrations happen in `FGameCoreEditorModule::StartupModule` and are unregistered in `ShutdownModule`.

---

## Module Registration

```cpp
void FGameCoreEditorModule::StartupModule()
{
    FPropertyEditorModule& PropertyModule =
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    // ULootTable — Sort Entries button
    PropertyModule.RegisterCustomClassLayout(
        ULootTable::StaticClass()->GetFName(),
        FOnGetDetailCustomizationInstance::CreateStatic(
            &FULootTableCustomization::MakeInstance));

    // FLootTableEntry — ILootRewardable-filtered asset picker
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
        PropertyModule->UnregisterCustomClassLayout(ULootTable::StaticClass()->GetFName());
        PropertyModule->UnregisterCustomPropertyTypeLayout(FLootTableEntry::StaticStruct()->GetFName());
    }
}
```

---

## Sub-pages

- [GameCoreEditorUtils](GameCore%20Editor/GameCoreEditorUtils.md)
