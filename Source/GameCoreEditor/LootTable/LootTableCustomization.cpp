#include "GameCoreEditor.h"
#include "LootTable/LootTableCustomization.h"

#include "IDetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailCategoryBuilder.h"
#include "ScopedTransaction.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#include "LootTable/ULootTable.h"
#include "LootTable/FLootTableEntry.h"

#define LOCTEXT_NAMESPACE "FULootTableCustomization"

TSharedRef<IDetailCustomization> FULootTableCustomization::MakeInstance()
{
    return MakeShared<FULootTableCustomization>();
}

void FULootTableCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    // Capture a raw pointer to the DetailBuilder; the button callback holds a
    // TWeakPtr so it does not extend the builder's lifetime.
    IDetailLayoutBuilder* DetailBuilderPtr = &DetailBuilder;

    // Add the "Sort Entries" button to the top of the "Loot Table" category.
    IDetailCategoryBuilder& Category =
        DetailBuilder.EditCategory(TEXT("Loot Table"));

    Category.AddCustomRow(LOCTEXT("SortEntriesRow", "Sort Entries"), /*bForAdvanced=*/false)
    .WholeRowContent()
    [
        SNew(SButton)
        .Text(LOCTEXT("SortEntriesButton", "Sort Entries"))
        .ToolTipText(LOCTEXT("SortEntriesTooltip",
            "Sorts the Entries array ascending by RollThreshold. "
            "Equivalent to the auto-sort that runs during IsDataValid."))
        .OnClicked_Lambda([this, DetailBuilderPtr]()
        {
            SortEntries(DetailBuilderPtr);
            return FReply::Handled();
        })
        .HAlign(HAlign_Center)
    ];
}

void FULootTableCustomization::SortEntries(IDetailLayoutBuilder* DetailBuilder)
{
    if (!DetailBuilder)
    {
        return;
    }

    // Retrieve the objects being customized (may be multiple in multi-edit).
    TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
    DetailBuilder->GetObjectsBeingCustomized(CustomizedObjects);

    if (CustomizedObjects.IsEmpty())
    {
        return;
    }

    // Open a scoped transaction so the sort is undoable.
    const FScopedTransaction Transaction(LOCTEXT("SortEntriesTransaction",
        "Sort Loot Table Entries"));

    for (const TWeakObjectPtr<UObject>& WeakObj : CustomizedObjects)
    {
        ULootTable* LootTable = Cast<ULootTable>(WeakObj.Get());
        if (!LootTable)
        {
            continue;
        }

        LootTable->Modify();

        // Sort ascending by RollThreshold — matches ULootTable::IsDataValid order and
        // the roller's scan direction (descending traversal finds highest qualifier).
        LootTable->Entries.Sort(
            [](const FLootTableEntry& A, const FLootTableEntry& B)
            {
                return A.RollThreshold < B.RollThreshold;
            });

        // Mark the package dirty so the editor knows there are unsaved changes.
        LootTable->MarkPackageDirty();
    }

    // Refresh the Details panel to reflect the new array order.
    DetailBuilder->ForceRefreshDetails();
}

#undef LOCTEXT_NAMESPACE
