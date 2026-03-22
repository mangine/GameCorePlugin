#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class IDetailChildrenBuilder;
class IPropertyTypeCustomizationUtils;

/**
 * FFLootTableEntryCustomization
 *
 * IPropertyTypeCustomization for FLootTableEntry. Iterates all children of the
 * struct and replaces the default asset picker for any property tagged with
 * meta = (GameCoreInterfaceFilter = "X") with a picker filtered to assets
 * implementing the named interface (e.g. ILootRewardable for "LootRewardable").
 *
 * This customization is registered in FGameCoreEditorModule::StartupModule on
 * FLootTableEntry. Because FLootTableEntry embeds FLootEntryReward, the
 * RewardDefinition property is reached via the nested struct child traversal
 * inside CustomizeChildren.
 *
 * Registered in FGameCoreEditorModule::StartupModule via:
 *   PropertyModule.RegisterCustomPropertyTypeLayout(
 *       FLootTableEntry::StaticStruct()->GetFName(),
 *       FOnGetPropertyTypeCustomizationInstance::CreateStatic(
 *           &FFLootTableEntryCustomization::MakeInstance));
 */
class GAMECOREEDITOR_API FFLootTableEntryCustomization : public IPropertyTypeCustomization
{
public:
    /** Factory function used by FPropertyEditorModule registration. */
    static TSharedRef<IPropertyTypeCustomization> MakeInstance();

    /**
     * Renders the struct header row using default behavior.
     * No customization is applied at the header level.
     */
    virtual void CustomizeHeader(
        TSharedRef<IPropertyHandle>      StructHandle,
        FDetailWidgetRow&                HeaderRow,
        IPropertyTypeCustomizationUtils& Utils) override;

    /**
     * Iterates all children of FLootTableEntry. Children without a
     * GameCoreInterfaceFilter meta tag are rendered with their default widget.
     * Children with the tag receive an interface-filtered SObjectPropertyEntryBox.
     *
     * The meta value names the interface without its leading U/I prefix, e.g.:
     *   meta = (GameCoreInterfaceFilter = "LootRewardable")
     * resolves to ULootRewardable::StaticClass().
     */
    virtual void CustomizeChildren(
        TSharedRef<IPropertyHandle>      StructHandle,
        IDetailChildrenBuilder&          ChildBuilder,
        IPropertyTypeCustomizationUtils& Utils) override;

private:
    /**
     * Recursively walks property handle children and adds filtered rows
     * for any property tagged with GameCoreInterfaceFilter.
     * Non-tagged leaf properties are added with default widgets.
     */
    void ProcessChildren(
        TSharedRef<IPropertyHandle>      ParentHandle,
        IDetailChildrenBuilder&          ChildBuilder,
        IPropertyTypeCustomizationUtils& Utils);
};
