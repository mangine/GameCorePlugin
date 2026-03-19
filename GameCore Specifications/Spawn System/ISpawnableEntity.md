# ISpawnableEntity

**Module:** `GameCore`
**File:** `GameCore/Source/GameCore/Spawning/ISpawnableEntity.h`

Marker interface. Any actor class that should be selectable in a `FSpawnEntry` asset picker must implement this interface. No method is mandatory — the single `BlueprintNativeEvent` has a no-op C++ default and is purely a post-spawn notification hook.

This interface deliberately carries no spawn-configuration data. All spawn parameters (count, rate, location strategy, requirements) live on the `USpawnManagerComponent` and `FSpawnEntry`, not on the entity.

---

## Declaration

```cpp
// File: GameCore/Source/GameCore/Spawning/ISpawnableEntity.h

UINTERFACE(MinimalAPI, Blueprintable)
class USpawnableEntity : public UInterface
{
    GENERATED_BODY()
};

class GAMECORE_API ISpawnableEntity
{
    GENERATED_BODY()
public:
    /**
     * Called by USpawnManagerComponent immediately after the actor is spawned
     * and before it is added to LiveInstances tracking.
     *
     * Use this to receive context from the spawner:
     * - Read loot table override from the manager
     * - Set AI parameters
     * - Apply faction assignment
     *
     * Anchor is the USpawnManagerComponent's owner actor (the spawn anchor).
     * Default C++ implementation is a no-op — not required to override.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Spawning")
    void OnSpawnedByManager(AActor* Anchor);
    virtual void OnSpawnedByManager_Implementation(AActor* Anchor) {}
};
```

---

## Implementing on an Actor

```cpp
// C++ — minimum required:
UCLASS()
class UMyNPC : public ACharacter, public ISpawnableEntity
{
    GENERATED_BODY()
    // No additional changes needed to be selectable in the picker.
};

// C++ — with override:
void UMyNPC::OnSpawnedByManager_Implementation(AActor* Anchor)
{
    USpawnManagerComponent* Mgr = Anchor
        ? Anchor->FindComponentByClass<USpawnManagerComponent>()
        : nullptr;
    if (!Mgr) return;

    // Apply loot table override if one is set for this class.
    TSoftObjectPtr<ULootTable> Override = Mgr->GetLootTableOverrideForClass(GetClass());
    if (!Override.IsNull())
        MyLootComp->SetLootTableOverride(Override);
}
```

In Blueprint: implement the `ISpawnableEntity` interface on any Actor Blueprint. Override the `OnSpawnedByManager` event if post-spawn setup is needed.

---

## Editor Asset Picker Filtering

`FSpawnEntry::EntityClass` is a `TSoftClassPtr<AActor>`. Without additional tooling the editor shows all actor classes — unusable for designers.

Filtering is enforced at authoring time by `FSpawnEntryCustomization`, an `IPropertyTypeCustomization` registered in `GameCoreEditor` for `FSpawnEntry`. It replaces the default `EntityClass` picker with an `SClassPropertyEntryBox` (or `SObjectPropertyEntryBox`) filtered via `GameCoreEditorUtils::AssetImplementsInterface(AssetData, USpawnableEntity::StaticClass())`.

Follow the exact same pattern as `FFLootEntryRewardCustomization`. Register in `FGameCoreEditorModule::StartupModule`:

```cpp
PropertyModule.RegisterCustomPropertyTypeLayout(
    FSpawnEntry::StaticStruct()->GetFName(),
    FOnGetPropertyTypeCustomizationInstance::CreateStatic(
        &FSpawnEntryCustomization::MakeInstance));
```

Add a row to the `GameCoreEditorUtils` consumers table when the customization is implemented.

---

## Notes

- `ISpawnableEntity` lives in the **runtime** `GameCore` module — implementing it on an entity class requires no editor dependency.
- `FSpawnEntryCustomization` lives in the **editor-only** `GameCoreEditor` module.
- The interface filter is an **authoring constraint only**. It prevents selecting incompatible classes in the editor. A runtime `Cast<ISpawnableEntity>` after spawn confirms eligibility before calling `OnSpawnedByManager`.
- `USpawnableEntity` uses `Blueprintable` so Blueprint actor classes can implement it without C++.
