# GameCoreEditorUtils

Shared editor utility namespace. Provides reusable building blocks for `IDetailCustomization` and `IPropertyTypeCustomization` implementations across the `GameCoreEditor` module.

**File:** `GameCoreEditor/Utils/GameCoreEditorUtils.h`

---

## Interface Filter

The most common shared utility. Produces a filter predicate for `SObjectPropertyEntryBox::OnShouldFilterAsset` that accepts only assets whose class implements a given UE interface.

```cpp
namespace GameCoreEditorUtils
{
    /**
     * Filter predicate for SObjectPropertyEntryBox::OnShouldFilterAsset.
     * Returns true (show asset) only if the asset's class implements InterfaceClass.
     *
     * Does NOT load the asset — reads class metadata from the Asset Registry only.
     * Safe to call for every asset in a picker without performance impact.
     *
     * @param AssetData       Candidate asset from the picker.
     * @param InterfaceClass  The UInterface class to check against.
     *                        Pass ULootRewardable::StaticClass(), UMyInterface::StaticClass(), etc.
     * @return                true if the asset should be shown; false to hide it.
     */
    static bool AssetImplementsInterface(
        const FAssetData& AssetData,
        UClass*           InterfaceClass)
    {
        UClass* AssetClass = AssetData.GetClass();
        if (!AssetClass) return false;
        return AssetClass->ImplementsInterface(InterfaceClass);
    }
}
```

---

## Usage Pattern

Bind `AssetImplementsInterface` to `OnShouldFilterAsset` via `CreateStatic`, passing the target interface as a payload:

```cpp
SNew(SObjectPropertyEntryBox)
    .PropertyHandle(MyPropertyHandle)
    .AllowedClass(UObject::StaticClass())
    .OnShouldFilterAsset_Static(
        &GameCoreEditorUtils::AssetImplementsInterface,
        UMyInterface::StaticClass())   // swap interface here per use case
    .ThumbnailPool(Utils.GetThumbnailPool())
```

To filter by a different interface in a new customization, change only the `UClass*` argument. No new predicate code required.

---

## Current Consumers

| Customization | Property | Interface |
|---|---|---|
| `FFLootTableEntryCustomization` | `FLootTableEntry::RewardDefinition` | `ULootRewardable` |

Add a row here when a new customization reuses the filter.

---

## Notes

- `AssetImplementsInterface` is `static` — no instance required, binds directly with `CreateStatic`.
- `FAssetData::GetClass()` resolves the `UClass` from Asset Registry metadata without loading the asset package. This is the correct approach for asset picker filters — never call `AssetData.GetAsset()` inside a filter predicate.
- The filter is an **authoring constraint only**. It prevents designers from selecting incompatible assets in the editor. It does not add a runtime type check — the fulfillment layer is responsible for safe casting after async load.
