#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"

/**
 * Shared editor utility functions for the GameCoreEditor module.
 * Header-only. Provides reusable building blocks for IDetailCustomization and
 * IPropertyTypeCustomization implementations across the GameCoreEditor module.
 */
namespace GameCoreEditorUtils
{
    /**
     * Filter predicate for SObjectPropertyEntryBox::OnShouldFilterAsset.
     * Returns true (show asset) only if the asset's class implements InterfaceClass.
     *
     * Does NOT load the asset — resolves class metadata from the Asset Registry only.
     * Safe to call for every asset in a picker with no performance impact.
     *
     * Bind via CreateStatic, passing the interface class as a payload argument:
     *   .OnShouldFilterAsset_Static(&GameCoreEditorUtils::AssetImplementsInterface,
     *                               UMyInterface::StaticClass())
     *
     * @param AssetData       Candidate asset from the picker.
     * @param InterfaceClass  The UInterface class to check against.
     *                        e.g. ULootRewardable::StaticClass()
     * @return                true = show asset (passes filter); false = hide it.
     */
    static bool AssetImplementsInterface(
        const FAssetData& AssetData,
        UClass*           InterfaceClass)
    {
        UClass* AssetClass = AssetData.GetClass();
        if (!AssetClass)
        {
            return false;
        }
        return AssetClass->ImplementsInterface(InterfaceClass);
    }

    /**
     * Collects all FAssetData entries in the Asset Registry whose native class
     * implements InterfaceClass. Does NOT load any assets.
     *
     * @param InterfaceClass  The UInterface class to filter by.
     * @param OutAssets       Populated with all matching asset data entries.
     */
    static void GetAssetsImplementingInterface(
        UClass*              InterfaceClass,
        TArray<FAssetData>&  OutAssets)
    {
        OutAssets.Reset();

        if (!InterfaceClass)
        {
            return;
        }

        const IAssetRegistry& AssetRegistry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry")).Get();

        TArray<FAssetData> AllAssets;
        AssetRegistry.GetAllAssets(AllAssets);

        for (const FAssetData& AssetData : AllAssets)
        {
            if (AssetImplementsInterface(AssetData, InterfaceClass))
            {
                OutAssets.Add(AssetData);
            }
        }
    }

} // namespace GameCoreEditorUtils
