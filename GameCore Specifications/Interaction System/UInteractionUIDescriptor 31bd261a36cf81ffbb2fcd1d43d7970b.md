# UInteractionUIDescriptor

**Sub-page of:** [Interaction System](../Interaction%20System%20317d261a36cf8196ae77fc3c2e1e352d.md)

`UInteractionUIDescriptor` is an **abstract `UObject` subclass** that encapsulates how a specific type of interactable actor populates the contextual UI panel shown when it is focused. One descriptor class exists per interaction category (NPC, item, helm, chest, etc.). Instances are **shared across all actors of the same type** — cached by `UInteractionDescriptorSubsystem` — so hundreds of actors referencing the same descriptor class result in exactly one object in memory.

Descriptors are **never replicated** and **never instantiated per actor**. They are stateless logic objects, not per-actor data containers.

**Files:** `Interaction/UI/InteractionUIDescriptor.h / .cpp`

---

# Design Rationale

`FInteractionEntryConfig` owns the **action data**: the verb (`Label`), the input action, the icon override. These are static, designer-authored, and identical across all machines.

Descriptors own the **contextual presentation logic**: reading the live actor’s name, stats, state, or any runtime data that the action config cannot know statically. The split keeps config immutable and descriptors the single coupling point to the live world object.

The widget itself stays **passive** — it exposes named slots and delegates population entirely to the descriptor. Widgets never branch on actor type. This mirrors the "Interaction Presenter" / "Context Provider" pattern used at studios like CD Projekt and Ubisoft.

---

# `FInteractionEntryConfig` Addition

A single field is added to `FInteractionEntryConfig` in `InteractionEntryConfig.h`:

```cpp
// Optional descriptor class for contextual UI population.
// One shared instance per class is cached by UInteractionDescriptorSubsystem.
// When set, UInteractionManagerComponent resolves the instance during ResolveOptions
// and stores it on FResolvedInteractionOption::UIDescriptor.
// Null is valid — the context panel stays hidden. No default is shipped by GameCore.
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI")
TSubclassOf<UInteractionUIDescriptor> UIDescriptorClass;
```

This is immutable config — never replicated, identical on all machines.

---

# Class Definition

```cpp
// Abstract base. Subclass in Blueprint or C++ per interaction category.
// Instances are stateless — do not store per-actor data here.
// All actor-contextual data must be read from the Interactable parameter at call time.
UCLASS(Abstract, Blueprintable, BlueprintType)
class GAMECORE_API UInteractionUIDescriptor : public UObject
{
    GENERATED_BODY()

public:
    // Called by the interaction widget when a new resolved option is focused.
    // Populate Widget with data drawn from Option (static config: label, icon, hold time,
    // input action) and Interactable (live actor: name, stats, level, inventory, etc.).
    //
    // Option.Label         — action verb ("Talk", "Loot", "Board")
    // Option.EntryIconOverride — entry-specific icon, may be null
    // Option.InputAction   — current binding for key/button display
    // Option.InputType     — Press or Hold
    // Option.HoldTimeSeconds — hold duration if InputType == Hold
    // Option.State         — Available, Locked, Occupied, Cooldown
    // Option.ConditionLabel — failure reason if State == Locked
    //
    // Interactable may be null if the actor was destroyed between resolve and display.
    // Implementations must null-check before reading from it.
    UFUNCTION(BlueprintNativeEvent, Category = "Interaction|UI")
    void PopulateContextWidget(
        UInteractionContextWidget* Widget,
        const FResolvedInteractionOption& Option,
        AActor* Interactable) const;
};
```

---

# `FResolvedInteractionOption` Addition

One field is added to `FResolvedInteractionOption` in `ResolvedInteractionOption.h`:

```cpp
// Resolved descriptor instance for this option. Shared — do not modify.
// Null when UIDescriptorClass is unset on the entry config.
// Widget calls UIDescriptor->PopulateContextWidget(...) if non-null.
UPROPERTY(BlueprintReadOnly)
TObjectPtr<UInteractionUIDescriptor> UIDescriptor;
```

Populated during `ResolveOptions()` in `UInteractionManagerComponent`:

```cpp
// When building each FResolvedInteractionOption in ResolveOptions:
if (Config->UIDescriptorClass)
{
    auto* Subsystem = GetGameInstance()->GetSubsystem<UInteractionDescriptorSubsystem>();
    Option.UIDescriptor = Subsystem->GetOrCreate(Config->UIDescriptorClass);
}
```

---

# Example Subclasses

| Descriptor | `PopulateContextWidget` behavior |
| --- | --- |
| `UNPCDescriptor` | Reads NPC name from an NPC component; shows faction, dialogue hint |
| `UItemDescriptor` | Reads item DataAsset from an item component; shows name, rarity, key stats |
| `UHelmDescriptor` | Reads ship health and crew requirement from a ship component |
| `UChestDescriptor` | Reads locked/unlocked state, loot tier from a chest component |

All subclasses are **game-specific** — they live in game code, not in GameCore. GameCore ships only the abstract base.

---

# Widget Contract

The interaction widget exposes a single entry point:

```cpp
// UInteractionContextWidget — game-side widget base class.
// Exposes named slots for the descriptor to populate.
// Implementation is fully game-specific.
UFUNCTION(BlueprintImplementableEvent, Category = "Interaction|UI")
void OnDescriptorPopulate(UInteractionUIDescriptor* Descriptor, AActor* Interactable);
```

The widget does **not** branch on actor type. It exposes slots (name text, stat rows, icon image, etc.) and the descriptor decides which slots to fill.

---

# Null Handling

- When `UIDescriptorClass` is null on the entry config, `Option.UIDescriptor` is null in the resolved option.
- The widget checks for null before calling `PopulateContextWidget` and hides the context panel gracefully.
- This matches the existing null-handling pattern for `IconDataAsset` and `EntryIconOverride`.

---

# Implementation Constraints

- **Descriptors must be stateless.** Do not cache actor references or per-frame data on a descriptor instance — the same instance is shared across all actors using that entry config.
- **`PopulateContextWidget` is called on the owning client only.** The descriptor never runs on the server.
- **The `Interactable` parameter may be null.** Always null-check before accessing actor data — the actor may have been destroyed between `ResolveOptions` and the widget update.
- **Async asset loads should be initiated by the descriptor, not the widget.** If populating a slot requires a soft asset load (e.g. an item DataAsset not yet in memory), the descriptor manages the async load and calls back to the widget when ready. The widget shows a loading state in the interim.