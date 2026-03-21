# ILootRewardable

Marker interface. Any asset that can appear as a `RewardDefinition` in a `FLootTableEntry` must implement this interface. The interface carries no methods — its presence on a class is the sole contract.

**File:** `LootTable/ILootRewardable.h`

---

## Interface Declaration

```cpp
// Marker interface — no methods required.
// Implement on any UObject-derived asset class to make it selectable
// in FLootEntryReward::RewardDefinition asset pickers.
UIINTERFACE(MinimalAPI, NotBlueprintable)
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
// One line — no hierarchy restructuring required.
UCLASS()
class UItemDefinition : public UPrimaryDataAsset, public ILootRewardable
{
    GENERATED_BODY()
    // ... existing fields unchanged
};
```

---

## Why a Marker Interface, Not a Base Class

Three approaches were evaluated for constraining `RewardDefinition` to compatible assets:

- **Subclassing `FPrimaryAssetId`**: rejected — plain struct, no UE reflection filtering benefit.
- **Shared base class `ULootRewardDefinition`**: rejected — UObject single inheritance. Any asset type with its own required base class (e.g. `UItemDefinition` inheriting from a game-specific base) cannot also inherit `ULootRewardDefinition` without restructuring its hierarchy. Unacceptable constraint for a generic plugin.
- **Marker interface `ILootRewardable`**: chosen — any class can implement an interface regardless of its inheritance chain. One line addition, zero disruption.

---

## Editor Asset Picker Filtering

The `FLootEntryReward::RewardDefinition` property is `TSoftObjectPtr<UObject>`. Without tooling, the editor shows all assets.

Filtering is enforced at authoring time by `FFLootEntryRewardCustomization` registered in `GameCoreEditor`. It replaces the default `RewardDefinition` picker with one filtered via `GameCoreEditorUtils::AssetImplementsInterface(ULootRewardable::StaticClass())`.

The `meta = (GameCoreInterfaceFilter = "LootRewardable")` tag on `FLootEntryReward::RewardDefinition` drives the customization. The pattern is reusable: any future struct property needing interface-filtered picking adds this meta tag and registers its own `IPropertyTypeCustomization` using the same `CustomizeChildren` pattern.

> **Runtime note:** The interface filter is an authoring-time constraint only. It is not enforced at runtime. The fulfillment layer is responsible for safe casting after async load.

---

## Notes

- `ILootRewardable` lives in the **runtime** `GameCore` module. Implementing it requires no editor dependency.
- `FFLootEntryRewardCustomization` lives in the **editor-only** `GameCoreEditor` module — never compiled into shipping builds.
- `TSoftObjectPtr<UObject>` is intentional at the C++ level because the constraint is interface-based, not type-based.
- If a non-`ILootRewardable` asset is present at runtime (hand-edited asset), `FLootReward::IsValid()` still returns true — `RewardType` drives routing. The fulfillment layer must cast safely.
