// Copyright GameCore Plugin. All Rights Reserved.

#include "HISMProxy/HISMProxyHostActorDetails.h"
#include "HISMProxy/HISMProxyHostActor.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Input/Reply.h"

// ─────────────────────────────────────────────────────────────────────────────
// MakeInstance
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<IDetailCustomization> FHISMProxyHostActorDetails::MakeInstance()
{
    return MakeShareable(new FHISMProxyHostActorDetails());
}

// ─────────────────────────────────────────────────────────────────────────────
// CustomizeDetails
// ─────────────────────────────────────────────────────────────────────────────

void FHISMProxyHostActorDetails::CustomizeDetails(
    IDetailLayoutBuilder& DetailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    if (Objects.IsEmpty()) { return; }

    AHISMProxyHostActor* HostActor = Cast<AHISMProxyHostActor>(Objects[0].Get());
    if (!HostActor) { return; }
    CachedHostActor = HostActor;

    IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("HISM Proxy");

    Category.AddCustomRow(FText::FromString("Validate Setup"))
    .WholeRowContent()
    [
        SNew(SButton)
        .Text(FText::FromString("Validate Setup"))
        .OnClicked(FOnClicked::CreateLambda([HostActor]()
        {
            if (HostActor) { HostActor->ValidateSetup(); }
            return FReply::Handled();
        }))
    ];

    AddInstanceTypeRows(DetailBuilder, HostActor);
}

// ─────────────────────────────────────────────────────────────────────────────
// AddInstanceTypeRows
// ─────────────────────────────────────────────────────────────────────────────

void FHISMProxyHostActorDetails::AddInstanceTypeRows(
    IDetailLayoutBuilder& DetailBuilder, AHISMProxyHostActor* HostActor)
{
    IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("HISM Proxy");

    for (int32 i = 0; i < HostActor->InstanceTypes.Num(); ++i)
    {
        const FHISMProxyInstanceType& Entry = HostActor->InstanceTypes[i];
        const int32 Count = Entry.HISM ? Entry.HISM->GetInstanceCount() : 0;

        AHISMProxyHostActor* CapturedActor = HostActor;
        const int32 CapturedIndex = i;

        Category.AddCustomRow(FText::FromString(Entry.TypeName.ToString()))
        .NameContent()
        [
            SNew(STextBlock).Text(FText::Format(
                INVTEXT("{0}  ({1} instances)"),
                FText::FromName(Entry.TypeName),
                FText::AsNumber(Count)))
        ]
        .ValueContent()
        [
            SNew(SButton)
            .Text(FText::FromString("Add Instance at Pivot"))
            .ToolTipText(FText::FromString(
                "Move this actor's pivot to the desired world position, then click."))
            // FOnClicked is TDelegate<FReply()> — zero args.
            // CreateLambda captures state by value. _Raw with trailing payload
            // does not compile for zero-argument delegates. See AD-12.
            .OnClicked(FOnClicked::CreateLambda(
                [CapturedActor, CapturedIndex]()
            {
                if (IsValid(CapturedActor))
                {
                    const FScopedTransaction Transaction(
                        FText::FromString("Add HISM Proxy Instance"));
                    CapturedActor->Modify();
                    CapturedActor->AddInstanceForType(
                        CapturedIndex, CapturedActor->GetActorTransform());

                    FPropertyEditorModule& PM =
                        FModuleManager::GetModuleChecked<FPropertyEditorModule>(
                            "PropertyEditor");
                    PM.NotifyCustomizationModuleChanged();
                }
                return FReply::Handled();
            }))
        ];
    }
}
