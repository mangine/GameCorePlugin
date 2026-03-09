# UInteractionIconDataAsset

**Sub-page of:** [Interaction System — Enhanced Specification](../Interaction%20System%20317d261a36cf8196ae77fc3c2e1e352d.md)

A plain `UDataAsset` subclass that maps each `EInteractableState` to a display icon. Referenced per-component via a soft pointer on `UInteractionComponent`. Icons are loaded on demand — never at world load or component initialization.

**Files:** `Interaction/Data/InteractionIconDataAsset.h / .cpp`

---

# Icon Resolution Chain

Before querying this asset, the widget must walk the full resolution chain in priority order:

```
1. FResolvedInteractionOption.ConditionIconOverride   — set by IInteractionConditionProvider
                                                         when the entry is Locked with a
                                                         condition-specific icon
2. FResolvedInteractionOption.EntryIconOverride        — per-entry override on FInteractionEntryConfig
3. UInteractionComponent::GetIconDataAsset()           — component-level asset (this class)
4. nullptr                                             — widget hides the icon slot
```

Each level falls through to the next if null. The widget is responsible for walking this chain — `UInteractionIconDataAsset` is only step 3.

---

# Class Definition

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UInteractionIconDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // All references are soft — icons load on demand when first requested by a widget.
    // The asset itself is lightweight: six soft pointers, no loaded textures at rest.

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
    TSoftObjectPtr<UTexture2D> AvailableIcon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
    TSoftObjectPtr<UTexture2D> OccupiedIcon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
    TSoftObjectPtr<UTexture2D> CooldownIcon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
    TSoftObjectPtr<UTexture2D> LockedIcon;

    // Disabled entries are filtered out during ResolveOptions and never reach the widget.
    // No icon field needed for Disabled state.

    // Returns the soft pointer for the given state.
    // Returns an empty soft pointer for Disabled or unknown states — callers must handle this.
    // Result is a soft pointer — call GetIconAsync (see below) to load before display.
    UFUNCTION(BlueprintCallable, Category = "Icons")
    TSoftObjectPtr<UTexture2D> GetIconForState(EInteractableState State) const;
};
```

```cpp
// InteractionIconDataAsset.cpp
TSoftObjectPtr<UTexture2D> UInteractionIconDataAsset::GetIconForState(
    EInteractableState State) const
{
    switch (State)
    {
        case EInteractableState::Available: return AvailableIcon;
        case EInteractableState::Occupied:  return OccupiedIcon;
        case EInteractableState::Cooldown:  return CooldownIcon;
        case EInteractableState::Locked:    return LockedIcon;
        default:                            return {};
    }
}
```

> **`GetIconForState` must be defined in the `.cpp`, not inline in the header.** UHT does not support inline `UFUNCTION` bodies. Defining it in the header will compile in some configurations but break UHT processing and produce cryptic header tool errors.
> 

---

# Async Loading

`GetIconForState` returns a `TSoftObjectPtr` — a path, not a loaded texture. Passing it directly to a widget's image brush will produce a blank result. The widget must request an async load first.

**C++ widget pattern:**

```cpp
void UInteractionPromptWidget::SetIconForState(
    const UInteractionIconDataAsset* Asset, EInteractableState State)
{
    if (!Asset) { IconImage->SetBrushFromTexture(nullptr); return; }

    const TSoftObjectPtr<UTexture2D> SoftIcon = Asset->GetIconForState(State);
    if (SoftIcon.IsNull()) { IconImage->SetBrushFromTexture(nullptr); return; }

    // If already loaded (cached from a previous request), use immediately.
    if (SoftIcon.IsValid())
    {
        IconImage->SetBrushFromTexture(SoftIcon.Get());
        return;
    }

    // Async load — widget remains blank until load completes.
    TWeakObjectPtr<UInteractionPromptWidget> WeakThis(this);
    UAssetManager::GetStreamableManager().RequestAsyncLoad(
        SoftIcon.ToSoftObjectPath(),
        [WeakThis, SoftIcon]()
        {
            if (UInteractionPromptWidget* W = WeakThis.Get())
                W->IconImage->SetBrushFromTexture(SoftIcon.Get());
        });
}
```

**Blueprint pattern:** Use the `Async Load Asset` node with the soft reference, then cast the loaded asset to `Texture2D` and assign to the image brush in the completion callback.

> **Icons load once and stay resident while anything holds a hard reference.** After first load, `SoftIcon.IsValid()` returns true and subsequent calls to `SetIconForState` for the same state return immediately without triggering a load. Icons are evicted only when nothing holds a hard reference — typically when the widget is destroyed. For frequently-visible icons (Available, Locked) consider pre-loading at widget construction.
> 

---

# Sharing & Overrides

Multiple `UInteractionComponent` instances across the world can reference the same `UInteractionIconDataAsset`. The soft pointer means the asset loads once and is shared — no per-component texture duplication regardless of how many interactables reference it.

The override chain (see Icon Resolution Chain above) means a shared asset is the baseline — individual entries can diverge without replacing the whole asset:

- A chest with a unique icon for its locked state sets `EntryIconOverride` on the entry config.
- A condition provider (level gate, faction check) sets `ConditionIconOverride` at runtime.
- Neither requires a new `UInteractionIconDataAsset`.

Reserve distinct assets for actors that need a **completely different visual language** across all states — a dangerous NPC, a puzzle element, a faction-specific interaction.

---

# Known Constraints

**No built-in default asset.** `UInteractionComponent::GetIconDataAsset()` returns `nullptr` when `IconDataAsset` is unset. There is no engine-level fallback. Widgets must handle null at step 3 of the resolution chain and hide the icon slot gracefully. Projects that want a consistent baseline should set a default asset on their base interactable Blueprint class so all derived actors inherit it.

**`Disabled` state has no icon by design.** Disabled entries are filtered out during `ResolveOptions` and never appear in `FResolvedInteractionOption`. The widget never receives them. Do not add a `DisabledIcon` field — it would never be read.

**Soft pointers in DataAssets are not cooked unless referenced.** If the asset is only referenced via a soft pointer on a component that is not always loaded, the icon textures may be missing in a cooked build. Use an Asset Manager primary asset type or an explicit cook rule to ensure icon textures are included.