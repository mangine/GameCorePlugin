# `UInteractionIconDataAsset`

**File:** `Interaction/UI/InteractionIconDataAsset.h/.cpp`

A plain `UDataAsset` that maps each `EInteractableState` to a soft-referenced `UTexture2D`. Referenced per-component via a soft pointer on `UInteractionComponent`. Icons load on demand — never at world load or component initialization.

---

## Icon Resolution Chain

Before querying this asset, the widget walks the full chain:

```
1. FResolvedInteractionOption.EntryIconOverride   — per-entry override (set in config)
2. UInteractionComponent::GetIconDataAsset()       — component-level asset (this class)
3. null                                            — widget hides the icon slot
```

This asset is step 2. The widget is responsible for walking this chain.

---

## Class Definition

```cpp
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Interaction/Enums/InteractionEnums.h"
#include "InteractionIconDataAsset.generated.h"

// Maps EInteractableState to display icons.
// All references are soft — icons load on demand. Asset itself is lightweight at rest.
UCLASS(BlueprintType)
class GAMECORE_API UInteractionIconDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
    TSoftObjectPtr<UTexture2D> AvailableIcon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
    TSoftObjectPtr<UTexture2D> OccupiedIcon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
    TSoftObjectPtr<UTexture2D> CooldownIcon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
    TSoftObjectPtr<UTexture2D> LockedIcon;

    // Disabled entries are never in FResolvedInteractionOption — no icon field needed.

    // Returns the soft pointer for the given state.
    // Returns empty for Disabled or unknown states.
    // Result is a soft pointer — caller must async load before display.
    UFUNCTION(BlueprintCallable, Category = "Icons")
    TSoftObjectPtr<UTexture2D> GetIconForState(EInteractableState State) const;
};
```

```cpp
// InteractionIconDataAsset.cpp
// NOTE: Must be defined in .cpp, not inline. UHT does not support inline UFUNCTION bodies.
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

---

## Async Loading (Widget Usage)

```cpp
void UInteractionPromptWidget::SetIconForState(
    const UInteractionIconDataAsset* Asset, EInteractableState State)
{
    if (!Asset) { IconImage->SetBrushFromTexture(nullptr); return; }

    const TSoftObjectPtr<UTexture2D> SoftIcon = Asset->GetIconForState(State);
    if (SoftIcon.IsNull()) { IconImage->SetBrushFromTexture(nullptr); return; }

    if (SoftIcon.IsValid())
    {
        IconImage->SetBrushFromTexture(SoftIcon.Get());
        return;
    }

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

---

## Constraints

- **No built-in default asset.** `GetIconDataAsset()` returns null when `IconDataAsset` is unset. Widgets must handle null and hide the slot gracefully. Projects wanting a baseline should set a default on their base interactable Blueprint class.
- **`Disabled` state has no icon by design.** Disabled entries are filtered during `ResolveOptions` and never reach the widget.
- **Soft pointers in DataAssets not cooked unless referenced.** Ensure icon textures are included via Asset Manager primary asset type or explicit cook rules.
