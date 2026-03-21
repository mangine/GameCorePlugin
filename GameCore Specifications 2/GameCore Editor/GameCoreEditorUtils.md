# GameCoreEditorUtils

**File:** `GameCoreEditor/Utils/GameCoreEditorUtils.h`

Shared editor utility namespace. Header-only. Provides reusable building blocks for `IDetailCustomization` and `IPropertyTypeCustomization` implementations across the `GameCoreEditor` module.

---

## `AssetImplementsInterface`

The most commonly used utility. Produces a filter predicate compatible with `SObjectPropertyEntryBox::OnShouldFilterAsset` that shows only assets whose class implements a given UE interface.

```cpp
namespace GameCoreEditorUtils
{
    /**
     * Filter predicate for SObjectPropertyEntryBox::OnShouldFilterAsset.
     * Returns true (show asset) only if the asset's class implements InterfaceClass.
     *
     * Does NOT load the asset — resolves class metadata from the Asset Registry only.
     * Safe to call for every asset in a picker with no performance impact.
     *
     * @param AssetData       Candidate asset from the picker.
     * @param InterfaceClass  The UInterface class to check against.
     *                        e.g. ULootRewardable::StaticClass(), UMyInterface::StaticClass()
     * @return                true = show asset; false = hide it.
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

Bind `AssetImplementsInterface` to `OnShouldFilterAsset` via `CreateStatic`, passing the interface class as a payload argument:

```cpp
SNew(SObjectPropertyEntryBox)
    .PropertyHandle(MyPropertyHandle)
    .AllowedClass(UObject::StaticClass())
    .OnShouldFilterAsset_Static(
        &GameCoreEditorUtils::AssetImplementsInterface,
        UMyInterface::StaticClass())   // swap interface here per use case
    .ThumbnailPool(Utils.GetThumbnailPool())
```

To filter by a different interface in a new customization, change only the `UClass*` argument. No new predicate required.

---

## Current Consumers

| Customization | Property | Interface |
|---|---|---|
| `FFLootTableEntryCustomization` | `FLootTableEntry::RewardDefinition` | `ILootRewardable` |

Add a row here when a new customization reuses this filter.

---

## Important Notes

- `AssetImplementsInterface` is `static` — no instance required; binds directly with `CreateStatic`.
- `FAssetData::GetClass()` resolves `UClass` from Asset Registry metadata **without loading the asset package**. This is the correct approach for picker filters — never call `AssetData.GetAsset()` inside a filter predicate.
- This filter is an **authoring-time constraint only**. It prevents designers from selecting incompatible assets. It does not enforce a runtime type check — callers must cast safely after async load.
