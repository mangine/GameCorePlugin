#include "GameCoreEditor.h"
#include "LootTable/LootTableEntryCustomization.h"

#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"

#include "Utils/GameCoreEditorUtils.h"

#include "LootTable/FLootTableEntry.h"

#define LOCTEXT_NAMESPACE "FFLootTableEntryCustomization"

TSharedRef<IPropertyTypeCustomization> FFLootTableEntryCustomization::MakeInstance()
{
    return MakeShared<FFLootTableEntryCustomization>();
}

void FFLootTableEntryCustomization::CustomizeHeader(
    TSharedRef<IPropertyHandle>      StructHandle,
    FDetailWidgetRow&                HeaderRow,
    IPropertyTypeCustomizationUtils& Utils)
{
    // Default header rendering — show the struct name / value summary.
    HeaderRow
    .NameContent()
    [
        StructHandle->CreatePropertyNameWidget()
    ]
    .ValueContent()
    [
        StructHandle->CreatePropertyValueWidget()
    ];
}

void FFLootTableEntryCustomization::CustomizeChildren(
    TSharedRef<IPropertyHandle>      StructHandle,
    IDetailChildrenBuilder&          ChildBuilder,
    IPropertyTypeCustomizationUtils& Utils)
{
    ProcessChildren(StructHandle, ChildBuilder, Utils);
}

void FFLootTableEntryCustomization::ProcessChildren(
    TSharedRef<IPropertyHandle>      ParentHandle,
    IDetailChildrenBuilder&          ChildBuilder,
    IPropertyTypeCustomizationUtils& Utils)
{
    uint32 NumChildren = 0;
    ParentHandle->GetNumChildren(NumChildren);

    for (uint32 i = 0; i < NumChildren; ++i)
    {
        TSharedRef<IPropertyHandle> ChildHandle =
            ParentHandle->GetChildHandle(i).ToSharedRef();

        // Check for the GameCoreInterfaceFilter metadata tag.
        const FString InterfaceName = ChildHandle->GetProperty()
            ? ChildHandle->GetProperty()->GetMetaData(TEXT("GameCoreInterfaceFilter"))
            : FString();

        if (!InterfaceName.IsEmpty())
        {
            // Convention: meta value has no U prefix — e.g. "LootRewardable" → ULootRewardable.
            UClass* InterfaceClass = FindObject<UClass>(
                ANY_PACKAGE, *FString::Printf(TEXT("U%s"), *InterfaceName));

            if (InterfaceClass)
            {
                // Replace the default picker with an interface-filtered SObjectPropertyEntryBox.
                ChildBuilder.AddCustomRow(ChildHandle->GetPropertyDisplayName())
                .NameContent()
                [
                    ChildHandle->CreatePropertyNameWidget()
                ]
                .ValueContent()
                [
                    SNew(SObjectPropertyEntryBox)
                        .PropertyHandle(ChildHandle)
                        .AllowedClass(UObject::StaticClass())
                        .OnShouldFilterAsset_Static(
                            &GameCoreEditorUtils::AssetImplementsInterface,
                            InterfaceClass)
                        .ThumbnailPool(Utils.GetThumbnailPool())
                ];
                continue;
            }
        }

        // No filter tag (or unresolvable interface name): render with the default widget.
        ChildBuilder.AddProperty(ChildHandle);
    }
}

#undef LOCTEXT_NAMESPACE
