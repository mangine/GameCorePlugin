# ILootRewardable

Marker interface. Any asset that can appear as a reward in a `FLootTableEntry` must implement this interface. The interface carries no methods — its presence on a class is the sole contract.

This approach avoids base class constraints: `UItemDefinition`, `UCurrencyDefinition`, `UAbilityDefinition`, and any other asset type can implement `ILootRewardable` regardless of their own inheritance hierarchy, with a single line of C++.

---

## Interface Declaration

**File:** `LootTable/ILootRewardable.h`

```cpp
// Marker interface — no methods required.
// Implement on any UObject-derived asset class to make it selectable
// in FLootTableEntry::RewardDefinition asset pickers.
UINTERFACE(MinimalAPI, NotBlueprintable)
class ULootRewardable : public UInterface
{
    GENERATED_BODY()
};

class GAMECORE_API ILootRewardable
{
    GENERATED_BODY()
    // Intentionally empty.
};
```

---

## Implementing on an Asset Type

```cpp
// In any system — no GameCore dependency required beyond the interface header.
UCLASS()
class UItemDefinition : public UPrimaryDataAsset, public ILootRewardable
{
    GENERATED_BODY()
    // ... item fields
};
```

One line. No restructuring of the existing hierarchy.

---

## Editor Asset Picker Filtering

`FLootTableEntry::RewardDefinition` uses a `TSoftObjectPtr<UObject>` in the struct declaration. Without additional tooling, the editor would show all assets — unusable for designers.

Filtering is provided by a **`IPropertyTypeCustomization`** registered in `GameCoreEditor` for `FLootTableEntry`. It replaces the default `RewardDefinition` picker with a filtered picker that only shows assets whose class implements `ILootRewardable`.

**File:** `GameCoreEditor/LootTable/FLootTableEntryCustomization.h`

```cpp
class FFLootTableEntryCustomization : public IPropertyTypeCustomization
{
public:
    static TSharedRef<IPropertyTypeCustomization> MakeInstance();

    virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
        FDetailWidgetRow& HeaderRow,
        IPropertyTypeCustomizationUtils& Utils) override;

    virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
        IDetailChildrenBuilder& ChildBuilder,
        IPropertyTypeCustomizationUtils& Utils) override;

private:
    // Builds an SObjectPropertyEntryBox filtered to ILootRewardable implementors.
    // Used for the RewardDefinition slot only; all other children rendered normally.
    TSharedRef<SWidget> BuildFilteredAssetPicker(
        TSharedRef<IPropertyHandle> RewardDefinitionHandle);

    // Filter callback passed to the asset picker.
    // Returns true only for assets whose class ImplementsInterface(ULootRewardable::StaticClass()).
    bool OnShouldFilterAsset(const FAssetData& AssetData) const;
};
```

Registered in the editor module startup:

```cpp
void FGameCoreEditorModule::StartupModule()
{
    FPropertyEditorModule& PropertyModule =
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    PropertyModule.RegisterCustomPropertyTypeLayout(
        FLootTableEntry::StaticStruct()->GetFName(),
        FOnGetPropertyTypeCustomizationInstance::CreateStatic(
            &FFLootTableEntryCustomization::MakeInstance));
}
```

---

## Reusability

`FFLootTableEntryCustomization::OnShouldFilterAsset` uses only `UClass::ImplementsInterface` — a generic UE mechanism. The same filter pattern can be extracted into a shared utility:

```cpp
// GameCoreEditor/Utils/GameCoreEditorUtils.h
namespace GameCoreEditorUtils
{
    // Returns a filter delegate that accepts only assets implementing the given interface.
    FOnShouldFilterAsset MakeInterfaceFilter(UClass* InterfaceClass);
}
```

Any future `IPropertyTypeCustomization` in GameCore that needs interface-filtered picking reuses `MakeInterfaceFilter` without duplicating the predicate logic.

---

## Notes

- `ILootRewardable` lives in the **runtime** `GameCore` module — implementing it requires no editor dependency.
- `FFLootTableEntryCustomization` lives in the **editor-only** `GameCoreEditor` module — never compiled into shipping builds.
- The soft reference in `FLootReward::RewardDefinition` remains `TSoftObjectPtr<UObject>` at the C++ level. The interface filter is an editor-only authoring constraint, not a runtime type check. The fulfillment layer casts to the expected type after loading.
- If a non-`ILootRewardable` asset is somehow loaded at runtime (e.g. hand-edited asset file), `FLootReward::IsValid()` still returns true — the `RewardType` tag drives routing. The fulfillment layer is responsible for safe casting.
