# `FResolvedInteractionOption`

**File:** `Interaction/ResolvedInteractionOption.h`

Client-side resolution output. Produced by `UInteractionComponent::ResolveOptions()`. Consumed exclusively by the UI layer. **Never replicated.** Must not be cached across frames — the owning buffer is reset on every re-resolve.

---

```cpp
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Interaction/Enums/InteractionEnums.h"
#include "ResolvedInteractionOption.generated.h"

class UInteractionComponent;
class UInputAction;
class UTexture2D;
class UInteractionUIDescriptor;

USTRUCT(BlueprintType)
struct GAMECORE_API FResolvedInteractionOption
{
    GENERATED_BODY()

    // The component that produced this option.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UInteractionComponent> SourceComponent;

    // Flat entry index within SourceComponent. Passed verbatim to ServerRequestInteract.
    UPROPERTY(BlueprintReadOnly)
    uint8 EntryIndex = 0;

    // ── Config Data (pointers / soft refs — no copies on the hot path) ─────────

    // Raw pointer into the config's Label FText. Valid for the frame the struct lives in.
    // NOT a UPROPERTY — raw pointers cannot be Blueprint-visible properties.
    // Blueprint widgets: use ConditionLabel when State == Locked and non-empty;
    // otherwise access Label through a BlueprintCallable C++ helper.
    const FText* Label = nullptr;

    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UInputAction> InputAction;

    // Per-entry icon override from FInteractionEntryConfig. May be null.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UTexture2D> EntryIconOverride;

    UPROPERTY(BlueprintReadOnly)
    EInteractionInputType InputType = EInteractionInputType::Press;

    UPROPERTY(BlueprintReadOnly)
    float HoldTimeSeconds = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag GroupTag;

    // ── Evaluated Runtime State ────────────────────────────────────────────────

    UPROPERTY(BlueprintReadOnly)
    EInteractableState State = EInteractableState::Available;

    // ── Requirement Failure Output (populated when State == Locked) ─────────────

    // FailureReason from FRequirementResult when EntryRequirements evaluation failed.
    // When non-empty and State == Locked, UI should show this instead of Label.
    // Examples: "Requires Golden Key", "Shop closes at dawn", "Level 20 required".
    UPROPERTY(BlueprintReadOnly)
    FText ConditionLabel;

    // ── UI Descriptor ──────────────────────────────────────────────────────────

    // Shared descriptor instance. Null when UIDescriptorClass is unset.
    // Widget calls UIDescriptor->PopulateContextWidget(...) if non-null.
    // Do NOT modify — shared across all actors using this entry config.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UInteractionUIDescriptor> UIDescriptor;
};
```

---

## Icon Resolution Order

The UI widget resolves the display icon using this priority chain (top-to-bottom, first non-null wins):

```
1. EntryIconOverride is non-null
       → use EntryIconOverride
         (applies in all states including Locked)

2. SourceComponent->GetIconDataAsset() is non-null
       → use IconDataAsset->GetIconForState(State)

3. No icon available
       → null — widget hides the icon slot gracefully
```

---

## Notes

- **Do not cache across frames.** The `SourceComponent` and `Label` pointers are only valid while the `ResolvedBuffer` they belong to is live. UI widgets must copy display values (text, state, hold time) into widget state on `OnResolvedOptionsChanged`.
- **`Label` is a raw pointer and cannot be a UPROPERTY.** UE reflection does not support raw pointers as Blueprint properties. Provide a `BlueprintCallable` C++ helper that dereferences it safely:

```cpp
UFUNCTION(BlueprintCallable, Category = "Interaction")
static FText GetOptionLabel(const FResolvedInteractionOption& Option)
{
    if (Option.State == EInteractableState::Locked && !Option.ConditionLabel.IsEmpty())
        return Option.ConditionLabel;
    return Option.Label ? *Option.Label : FText::GetEmpty();
}
```
