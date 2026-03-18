# ILootRewardable

Marker interface. Any asset that can appear as a reward in a `FLootTableEntry` must implement this interface. The interface carries no methods — its presence on a class is the sole contract.

This approach avoids base class constraints: `UItemDefinition`, `UCurrencyDefinition`, `UAbilityDefinition`, and any other asset type can implement `ILootRewardable` regardless of their own inheritance hierarchy, with a single line of C++.

---

## Interface Declaration

**File:** `LootTable/ILootRewardable.h`

```cpp
// Marker interface — no methods required.
// Implement on any UObject-derived asset class to make it selectable
// in FLootEntryReward::RewardDefinition asset pickers.
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

`FLootEntryReward::RewardDefinition` is a `TSoftObjectPtr<UObject>`. Without additional tooling the editor shows all assets — unusable for designers.

Filtering is enforced at authoring time by `FFLootEntryRewardCustomization`, an `IPropertyTypeCustomization` registered in `GameCoreEditor` for `FLootEntryReward`. It replaces the default `RewardDefinition` picker with an `SObjectPropertyEntryBox` filtered via `GameCoreEditorUtils::AssetImplementsInterface` — see [GameCoreEditorUtils](../GameCore%20Editor/GameCoreEditorUtils.md).

### `FFLootEntryRewardCustomization`

**File:** `GameCoreEditor/LootTable/FFLootEntryRewardCustomization.h`

```cpp
class FFLootEntryRewardCustomization : public IPropertyTypeCustomization
{
public:
    static TSharedRef<IPropertyTypeCustomization> MakeInstance();

    // Renders the struct row header — default behavior.
    virtual void CustomizeHeader(
        TSharedRef<IPropertyHandle>      StructHandle,
        FDetailWidgetRow&                HeaderRow,
        IPropertyTypeCustomizationUtils& Utils) override;

    // Iterates all children of FLootEntryReward.
    // Children without GameCoreInterfaceFilter meta are rendered normally.
    // Children with the tag receive an ILootRewardable-filtered asset picker.
    virtual void CustomizeChildren(
        TSharedRef<IPropertyHandle>      StructHandle,
        IDetailChildrenBuilder&          ChildBuilder,
        IPropertyTypeCustomizationUtils& Utils) override;
};
```

### `CustomizeChildren` Implementation

```cpp
void FFLootEntryRewardCustomization::CustomizeChildren(
    TSharedRef<IPropertyHandle>      StructHandle,
    IDetailChildrenBuilder&          ChildBuilder,
    IPropertyTypeCustomizationUtils& Utils)
{
    uint32 NumChildren = 0;
    StructHandle->GetNumChildren(NumChildren);

    for (uint32 i = 0; i < NumChildren; ++i)
    {
        TSharedRef<IPropertyHandle> ChildHandle =
            StructHandle->GetChildHandle(i).ToSharedRef();

        const FString InterfaceName = ChildHandle->GetProperty()
            ? ChildHandle->GetProperty()->GetMetaData(TEXT("GameCoreInterfaceFilter"))
            : FString();

        if (!InterfaceName.IsEmpty())
        {
            // Resolve UClass from meta value. Convention: no U prefix in meta value.
            // e.g. "LootRewardable" → ULootRewardable::StaticClass()
            UClass* InterfaceClass = FindObject<UClass>(
                ANY_PACKAGE, *FString::Printf(TEXT("U%s"), *InterfaceName));

            if (InterfaceClass)
            {
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

        ChildBuilder.AddProperty(ChildHandle);
    }
}
```

### Registration

```cpp
// In FGameCoreEditorModule::StartupModule()
PropertyModule.RegisterCustomPropertyTypeLayout(
    FLootEntryReward::StaticStruct()->GetFName(),
    FOnGetPropertyTypeCustomizationInstance::CreateStatic(
        &FFLootEntryRewardCustomization::MakeInstance));
```

Registered on `FLootEntryReward` — the struct that directly owns `RewardDefinition`. This ensures the customization sees the field as a direct child, with no need to recurse through nested structs.

---

## Reusability

| Component | What it does | How to reuse |
|---|---|---|
| `GameCoreEditorUtils::AssetImplementsInterface` | Filter predicate | Pass any `UClass*` as the interface argument |
| `meta = (GameCoreInterfaceFilter = "X")` | Marks a property for filtered picking | Add to any `TSoftObjectPtr<UObject>` in any customized struct |
| `CustomizeChildren` pattern | Reads meta tag, wires filter | Register a new `IPropertyTypeCustomization` for a different struct; copy the pattern |

A new struct needing interface-filtered picking requires: one `IPropertyTypeCustomization`, one `RegisterCustomPropertyTypeLayout` call, and the `meta` tag on the target property. `GameCoreEditorUtils` needs no changes.

---

## Notes

- `ILootRewardable` lives in the **runtime** `GameCore` module — implementing it requires no editor dependency.
- `FFLootEntryRewardCustomization` lives in the **editor-only** `GameCoreEditor` module — never compiled into shipping builds.
- The `TSoftObjectPtr<UObject>` type is intentional. The interface filter is an authoring constraint only, not a runtime type check. The fulfillment layer casts after async load.
- If a non-`ILootRewardable` asset is somehow present at runtime (hand-edited asset file), `FLootReward::IsValid()` still returns true — `RewardType` drives routing. The fulfillment layer is responsible for safe casting.
