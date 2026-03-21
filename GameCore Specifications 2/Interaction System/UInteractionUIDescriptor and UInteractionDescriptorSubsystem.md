# `UInteractionUIDescriptor` and `UInteractionDescriptorSubsystem`

**Files:** `Interaction/UI/InteractionUIDescriptor.h/.cpp` | `Interaction/UI/InteractionDescriptorSubsystem.h/.cpp`

Descriptors bridge the gap between **static entry config** and **live actor data** for the contextual UI panel shown when an interactable is focused. `UInteractionDescriptorSubsystem` caches exactly one descriptor instance per class for the session lifetime.

---

## Design

`FInteractionEntryConfig` owns action data — label, input action, icon override, hold time. Static, never replicated.

`UInteractionUIDescriptor` owns contextual presentation logic — reads the live actor at focus time (NPC name, item stats, ship condition) and populates named widget slots. **Stateless — the same instance is shared across all actors using that entry config.**

Descriptors are game-code subclasses. GameCore ships only the abstract base. All subclasses live in game code, not in the plugin.

---

## `UInteractionUIDescriptor`

```cpp
// File: Interaction/UI/InteractionUIDescriptor.h
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interaction/ResolvedInteractionOption.h"
#include "InteractionUIDescriptor.generated.h"

class UInteractionContextWidget;

// Abstract base. Subclass in Blueprint or C++ per interaction category.
// Instances are STATELESS — do not store per-actor data here.
// All actor-contextual data must be read from Interactable at call time.
UCLASS(Abstract, Blueprintable, BlueprintType)
class GAMECORE_API UInteractionUIDescriptor : public UObject
{
    GENERATED_BODY()

public:
    // Called by the interaction widget when a resolved option is focused.
    // Populate Widget with data from Option (static config) and Interactable (live actor).
    //
    // Option fields:
    //   Label, EntryIconOverride, InputAction, InputType, HoldTimeSeconds,
    //   State, ConditionLabel
    //
    // Interactable may be null — actor may be destroyed between resolve and display.
    // Always null-check before reading from it.
    UFUNCTION(BlueprintNativeEvent, Category = "Interaction|UI")
    void PopulateContextWidget(
        UInteractionContextWidget* Widget,
        const FResolvedInteractionOption& Option,
        AActor* Interactable) const;
};
```

### Implementation Rules

- **Descriptors must be stateless.** Never cache actor references or per-frame data — the same instance is shared across all actors using that entry config.
- **`PopulateContextWidget` is called on the owning client only.** Never runs on the server.
- **`Interactable` may be null.** Always null-check before accessing actor data.
- **Async asset loads should be initiated by the descriptor**, not the widget. If populating a slot requires a soft asset load, the descriptor manages the async load and calls back to the widget when ready.

---

## `UInteractionDescriptorSubsystem`

```cpp
// File: Interaction/UI/InteractionDescriptorSubsystem.h
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "InteractionDescriptorSubsystem.generated.h"

class UInteractionUIDescriptor;

// Caches one UInteractionUIDescriptor instance per class for the game session.
// Sole allocation site for descriptor objects — nothing else calls NewObject<UInteractionUIDescriptor>.
UCLASS()
class GAMECORE_API UInteractionDescriptorSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // Returns the shared descriptor instance for Class.
    // Creates and caches it on first call. Returns null if Class is null.
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    UInteractionUIDescriptor* GetOrCreate(TSubclassOf<UInteractionUIDescriptor> Class);

    // Removes a cached descriptor, forcing re-creation on next GetOrCreate.
    // For hot-reload and editor utility use only.
    void Invalidate(TSubclassOf<UInteractionUIDescriptor> Class);

    void ClearAll();

protected:
    virtual void Deinitialize() override;

private:
    // One entry per class. Outer is this subsystem (GC root).
    UPROPERTY()
    TMap<TSubclassOf<UInteractionUIDescriptor>, TObjectPtr<UInteractionUIDescriptor>> Cache;
};
```

```cpp
// InteractionDescriptorSubsystem.cpp
UInteractionUIDescriptor* UInteractionDescriptorSubsystem::GetOrCreate(
    TSubclassOf<UInteractionUIDescriptor> Class)
{
    if (!Class) return nullptr;

    if (TObjectPtr<UInteractionUIDescriptor>* Found = Cache.Find(Class))
        return *Found;

    UInteractionUIDescriptor* New = NewObject<UInteractionUIDescriptor>(this, Class);
    Cache.Add(Class, New);
    return New;
}

void UInteractionDescriptorSubsystem::Invalidate(
    TSubclassOf<UInteractionUIDescriptor> Class)
{
    Cache.Remove(Class);
}

void UInteractionDescriptorSubsystem::ClearAll()
{
    Cache.Empty();
}

void UInteractionDescriptorSubsystem::Deinitialize()
{
    ClearAll();
    Super::Deinitialize();
}
```

### Memory Profile

| Scenario | Descriptor instances |
|---|---|
| 300 NPCs using `UNPCDescriptor` | **1** |
| 200 chests using `UChestDescriptor` | **1** |
| N actors, K distinct descriptor classes | **K** |

### Constraints

- **Instances created lazily.** Pre-warm by calling `GetOrCreate` for known classes during loading if deterministic memory layout is required.
- **Not thread-safe.** `ResolveOptions` (and thus `GetOrCreate`) runs on the game thread only.
- **Subsystem pointer is cached at `BeginPlay`** in `UInteractionManagerComponent`. Zero subsystem-lookup overhead on the `ResolveOptions` hot path.
