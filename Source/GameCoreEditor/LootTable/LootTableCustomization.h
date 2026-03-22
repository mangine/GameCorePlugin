#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

/**
 * FULootTableCustomization
 *
 * IDetailCustomization for ULootTable. Adds a "Sort Entries" button to the
 * Details panel that sorts ULootTable::Entries by RollThreshold ascending,
 * marks the asset dirty, and refreshes the Details panel.
 *
 * Registered in FGameCoreEditorModule::StartupModule via:
 *   PropertyModule.RegisterCustomClassLayout(
 *       ULootTable::StaticClass()->GetFName(),
 *       FOnGetDetailCustomizationInstance::CreateStatic(
 *           &FULootTableCustomization::MakeInstance));
 */
class GAMECOREEDITOR_API FULootTableCustomization : public IDetailCustomization
{
public:
    /** Factory function used by FPropertyEditorModule registration. */
    static TSharedRef<IDetailCustomization> MakeInstance();

    /** Injects the "Sort Entries" button row into the Details panel. */
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    /**
     * Callback invoked when the "Sort Entries" button is clicked.
     * Sorts Entries ascending by RollThreshold, marks the outer package dirty,
     * and forces a Details panel refresh.
     *
     * @param DetailBuilder  Non-owning pointer captured via WeakPtr in the delegate.
     */
    void SortEntries(IDetailLayoutBuilder* DetailBuilder);
};
