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

`FLootTableEntry::RewardDefinition` is a `TSoftObjectPtr<UObject>` at the C++ level. Without additional tooling the editor would show all assets — unusable for designers.

Filtering is enforced at authoring time by `FFLootTableEntryCustomization`, an `IPropertyTypeCustomization` registered in `GameCoreEditor`. It replaces the default `RewardDefinition` picker with an `SObjectPropertyEntryBox` filtered via `GameCoreEditorUtils::AssetImplementsInterface` — see [GameCoreEditorUtils](../GameCore%20Editor/GameCoreEditorUtils.md).

### `FLootTableEntry` — Property Hook

For `FFLootTableEntryCustomization` to intercept the right field, `FLootTableEntry::RewardDefinition` carries a `meta` tag that marks it as the target for custom picker rendering:

```cpp
// In FLootTableEntry — the meta tag signals FFLootTableEntryCustomization
// to replace this field's widget with the ILootRewardable-filtered picker.
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward",
    meta = (GameCoreInterfaceFilter = "LootRewardable"))
TSoftObjectPtr<UObject> RewardDefinition;
```

`FFLootTableEntryCustomization::CustomizeChildren` checks for this meta tag to identify which child property gets the filtered picker. All other children are rendered normally. This makes the mechanism explicit and reusable — any future struct property that needs interface filtering adds the same meta tag and the customization handles it automatically.

### `FFLootTableEntryCustomization`

**File:** `GameCoreEditor/LootTable/FLootTableEntryCustomization.h`

```cpp
class FFLootTableEntryCustomization : public IPropertyTypeCustomization
{
public:
    static TSharedRef<IPropertyTypeCustomization> MakeInstance();

    // Renders the struct row header — default behavior, no override needed.
    virtual void CustomizeHeader(
        TSharedRef<IPropertyHandle>      StructHandle,
        FDetailWidgetRow&                HeaderRow,
        IPropertyTypeCustomizationUtils& Utils) override;

    // Iterates all children of FLootTableEntry.
    // Children without the GameCoreInterfaceFilter meta tag are added normally.
    // Children with the tag get a filtered SObjectPropertyEntryBox instead.
    virtual void CustomizeChildren(
        TSharedRef<IPropertyHandle>      StructHandle,
        IDetailChildrenBuilder&          ChildBuilder,
        IPropertyTypeCustomizationUtils& Utils) override;
};
```

### `CustomizeChildren` Implementation

```cpp
void FFLootTableEntryCustomization::CustomizeChildren(
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

        // Check for the interface filter meta tag.
        const FString InterfaceName = ChildHandle->GetProperty()
            ? ChildHandle->GetProperty()->GetMetaData(TEXT("GameCoreInterfaceFilter"))
            : FString();

        if (!InterfaceName.IsEmpty())
        {
            // Resolve the interface UClass by name from the meta value.
            // Convention: meta value is the interface class name without the U prefix.
            // e.g. "LootRewardable" resolves to ULootRewardable::StaticClass().
            UClass* InterfaceClass = FindObject<UClass>(
                ANY_PACKAGE, *FString::Printf(TEXT("U%s"), *InterfaceName));

            if (InterfaceClass)
            {
                // Replace default widget with a filtered asset picker.
                ChildBuilder.AddCustomRow(
                    ChildHandle->GetPropertyDisplayName())
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

        // Default rendering for all other children.
        ChildBuilder.AddProperty(ChildHandle);
    }
}
```

**Key points:**
- The meta value `"LootRewardable"` is resolved to `ULootRewardable::StaticClass()` at customization time via `FindObject` — no hardcoded class references in the customization itself.
- Adding the meta tag to any future `TSoftObjectPtr<UObject>` property in any struct registered with this customization pattern automatically gets the filtered picker — no changes to the customization code.
- `GameCoreEditorUtils::AssetImplementsInterface` does the actual filtering — see [GameCoreEditorUtils](../GameCore%20Editor/GameCoreEditorUtils.md).

### Registration

```cpp
// In FGameCoreEditorModule::StartupModule()
PropertyModule.RegisterCustomPropertyTypeLayout(
    FLootTableEntry::StaticStruct()->GetFName(),
    FOnGetPropertyTypeCustomizationInstance::CreateStatic(
        &FFLootTableEntryCustomization::MakeInstance));
```

---

## Reusability

The entire filtering mechanism is interface-agnostic:

| Component | What it does | How to reuse |
|---|---|---|
| `GameCoreEditorUtils::AssetImplementsInterface` | Filter predicate | Pass any `UClass*` as the interface argument |
| `meta = (GameCoreInterfaceFilter = "X")` | Marks a property for filtered picking | Add to any `TSoftObjectPtr<UObject>` property in any customized struct |
| `FFLootTableEntryCustomization::CustomizeChildren` | Reads the meta tag and wires the filter | Register a new `IPropertyTypeCustomization` for a different struct; same `CustomizeChildren` pattern |

A new struct that needs interface-filtered picking requires: one new `IPropertyTypeCustomization` class (copy the `CustomizeChildren` pattern), one `RegisterCustomPropertyTypeLayout` call, and the `meta` tag on the target property. `GameCoreEditorUtils` needs no changes.

---

## Notes

- `ILootRewardable` lives in the **runtime** `GameCore` module — implementing it requires no editor dependency.
- `FFLootTableEntryCustomization` lives in the **editor-only** `GameCoreEditor` module — never compiled into shipping builds.
- The `TSoftObjectPtr<UObject>` type on `RewardDefinition` is intentional. The interface filter is an editor authoring constraint, not a C++ type constraint. The fulfillment layer casts to the expected type after async load.
- If a non-`ILootRewardable` asset is hand-edited into an asset file at runtime, `FLootReward::IsValid()` still returns true — `RewardType` drives routing. The fulfillment layer handles safe casting.
