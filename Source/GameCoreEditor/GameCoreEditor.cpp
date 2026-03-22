#include "GameCoreEditor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

// Forward declarations — customizations are registered here and defined in their respective files
class FULootTableCustomization;
class FFLootTableEntryCustomization;

// LootTable customization includes
#include "LootTable/LootTableCustomization.h"
#include "LootTable/LootTableEntryCustomization.h"

// LootTable runtime types (for registration keys)
#include "LootTable/ULootTable.h"
#include "LootTable/FLootTableEntry.h"

// HISMProxy editor tooling
#include "HISMProxy/HISMProxyHostActorDetails.h"
#include "HISMProxy/HISMProxyHostActor.h"

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

    // AHISMProxyHostActor — Validate Setup + Add Instance at Pivot buttons
    PropertyModule.RegisterCustomClassLayout(
        AHISMProxyHostActor::StaticClass()->GetFName(),
        FOnGetDetailCustomizationInstance::CreateStatic(
            &FHISMProxyHostActorDetails::MakeInstance));

    PropertyModule.NotifyCustomizationModuleChanged();
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

        // Must unregister — omitting causes a crash on editor plugin reload.
        PropertyModule->UnregisterCustomClassLayout(
            AHISMProxyHostActor::StaticClass()->GetFName());
    }
}

IMPLEMENT_MODULE(FGameCoreEditorModule, GameCoreEditor)
