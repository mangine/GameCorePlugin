# FFLootEntryRewardCustomization

`IPropertyTypeCustomization`. Editor-only. Replaces the default `FLootEntryReward::RewardDefinition` asset picker with one filtered to assets implementing `ILootRewardable`.

**File:** `GameCoreEditor/LootTable/FFLootEntryRewardCustomization.h`

Module: `GameCoreEditor` — never compiled into shipping builds.

---

## Declaration

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

---

## `CustomizeChildren` Implementation

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
            // Convention: meta value has no U prefix — e.g. "LootRewardable" → ULootRewardable
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

---

## Registration

```cpp
// In FGameCoreEditorModule::StartupModule()
PropertyModule.RegisterCustomPropertyTypeLayout(
    FLootEntryReward::StaticStruct()->GetFName(),
    FOnGetPropertyTypeCustomizationInstance::CreateStatic(
        &FFLootEntryRewardCustomization::MakeInstance));
```

Registered on `FLootEntryReward` — the struct that directly owns `RewardDefinition`. The customization sees the field as a direct child, no nesting traversal required.

---

## Reusability

This pattern is reusable for any struct needing interface-filtered asset picking:

| Component | What it does | How to reuse |
|---|---|---|
| `GameCoreEditorUtils::AssetImplementsInterface` | Filter predicate | Pass any `UClass*` as the interface argument |
| `meta = (GameCoreInterfaceFilter = "X")` | Marks a property for filtered picking | Add to any `TSoftObjectPtr<UObject>` in any customized struct |
| `CustomizeChildren` pattern | Reads meta tag, wires filter | Register a new `IPropertyTypeCustomization` for the target struct; copy this pattern |

`GameCoreEditorUtils::AssetImplementsInterface` needs no changes for new uses.
