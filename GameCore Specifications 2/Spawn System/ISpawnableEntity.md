# ISpawnableEntity

**Module:** `GameCore`  
**File:** `GameCore/Source/GameCore/Spawning/ISpawnableEntity.h`

Marker interface. Any actor class that should appear in the `FSpawnEntry` asset picker must implement this interface. The single `BlueprintNativeEvent` has a no-op C++ default and serves purely as a post-spawn notification hook.

This interface deliberately carries no spawn-configuration data. All spawn parameters (count, rate, location strategy, requirements) live on `USpawnManagerComponent` and `FSpawnEntry`, not on the entity.

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
     * Called by USpawnManagerComponent immediately after the actor finishes
     * spawning (post FinishSpawning) and before it is added to LiveInstances
     * tracking.
     *
     * Use this to receive context from the spawner:
     *   - Read the loot table override via Mgr->GetLootTableOverrideForClass
     *   - Set AI parameters / faction / difficulty tier
     *
     * Anchor is the USpawnManagerComponent's owner actor.
     * Default C++ implementation is a no-op — overriding is not required.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Spawning")
    void OnSpawnedByManager(AActor* Anchor);
    virtual void OnSpawnedByManager_Implementation(AActor* Anchor) {}
};
```

---

## Minimum Implementation (C++)

```cpp
// No override needed to be selectable in the picker:
UCLASS()
class UMyNPC : public ACharacter, public ISpawnableEntity
{
    GENERATED_BODY()
};
```

## Full Implementation (C++)

```cpp
void UMyNPC::OnSpawnedByManager_Implementation(AActor* Anchor)
{
    USpawnManagerComponent* Mgr = Anchor
        ? Anchor->FindComponentByClass<USpawnManagerComponent>()
        : nullptr;
    if (!Mgr) return;

    TSoftObjectPtr<ULootTable> Override = Mgr->GetLootTableOverrideForClass(GetClass());
    if (!Override.IsNull())
        LootComp->SetLootTableOverride(Override);
}
```

## Blueprint

In any Actor Blueprint, open *Class Settings → Implemented Interfaces* and add `ISpawnableEntity`. Override the `OnSpawnedByManager` event node if post-spawn setup is needed. No additional C++ is required.

---

## Editor Asset Picker Filtering

`FSpawnEntry::EntityClass` is a `TSoftClassPtr<AActor>`. Without additional tooling the editor shows all actor classes — unusable for designers.

Filtering is enforced at authoring time by `FSpawnEntryCustomization` (registered in `GameCoreEditor`). It replaces the default class picker with one filtered via `GameCoreEditorUtils::AssetImplementsInterface(AssetData, USpawnableEntity::StaticClass())`.

See the `FSpawnEntryCustomization` spec for full registration and implementation details.

---

## Notes

- `ISpawnableEntity` lives in the **runtime** `GameCore` module — no editor dependency for implementors.
- The `Blueprintable` flag on `USpawnableEntity` allows Blueprint actor classes to implement it without C++.
- The interface filter is an **authoring constraint only**. At runtime, `USpawnManagerComponent::TrySpawnForEntry` confirms eligibility via `Actor->Implements<USpawnableEntity>()` before calling `Execute_OnSpawnedByManager`.
- `USpawnManagerComponent` calls `Execute_OnSpawnedByManager` — always use the `Execute_` prefix when calling `BlueprintNativeEvent` functions on interface pointers from C++.
