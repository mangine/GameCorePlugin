# UHighlightComponent

**Sub-page of:** [Interaction System](../Interaction%20System%20317d261a36cf8196ae77fc3c2e1e352d.md)

`UHighlightComponent` is an **opt-in, client-side presentation component** added to any actor that should visually indicate it is interactable when focused by the player's scanner. It is fully decoupled from `UInteractionComponent` — an actor may have both, but neither requires the other.

Activation is driven exclusively by `UInteractionManagerComponent` on the owning client, via the `OnBestInteractableChanged` delegate. The server and non-owning clients never touch this component.

**Files:** `Interaction/Components/HighlightComponent.h / .cpp`

---

# Design Rationale

Highlighting is presentation, not interaction logic. Keeping it in a separate component:

- Preserves `UInteractionComponent` as a pure data container and state broadcaster.
- Allows highlighting non-interactable actors in the future (quest targets, hostile NPCs in detection range, etc.) without touching the interaction system.
- Keeps the stencil value configurable per-actor type, enabling the post-process shader to color-code different categories independently.

The implementation uses **Custom Depth + Stencil** rather than per-material highlight overlays. This approach has zero material permutation cost, works identically on Skeletal and Static meshes without touching any material, and the toggle is a single `SetRenderCustomDepth` call per primitive — the cheapest possible runtime change.

---

# Class Definition

```cpp
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UHighlightComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    // Stencil value written to CustomDepth when highlight is active.
    // Consumed by the post-process outline material to determine highlight color/style.
    // Recommended convention:
    //   1 = generic interactable
    //   2 = NPC
    //   3 = item / loot
    //   4 = quest objective
    // Values are project-defined — GameCore ships no default post-process asset.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Highlight",
        meta = (ClampMin = "1", ClampMax = "255"))
    uint8 StencilValue = 1;

    // Activate or deactivate the highlight on all owned primitives.
    // Called by UInteractionManagerComponent when the best interactable changes.
    // Safe to call with the current state (no-op if already in requested state).
    UFUNCTION(BlueprintCallable, Category = "Highlight")
    void SetHighlightActive(bool bActive);

    // Returns whether the highlight is currently active.
    UFUNCTION(BlueprintPure, Category = "Highlight")
    bool IsHighlightActive() const { return bHighlightActive; }

private:
    virtual void BeginPlay() override;

    // Cached at BeginPlay — never queried at runtime.
    // Includes all UPrimitiveComponents on the owning actor at spawn time.
    // Dynamically attached primitives added after BeginPlay are not covered.
    TArray<TObjectPtr<UPrimitiveComponent>> OwnedPrimitives;

    bool bHighlightActive = false;
};
```

---

# BeginPlay — Primitive Cache

```cpp
void UHighlightComponent::BeginPlay()
{
    Super::BeginPlay();

    // Only relevant on owning client — but caching is harmless on other machines
    // since SetHighlightActive is never called there.
    AActor* Owner = GetOwner();
    if (!Owner) return;

    Owner->GetComponents<UPrimitiveComponent>(OwnedPrimitives);

    // Ensure CustomDepth is off at spawn regardless of asset defaults.
    for (UPrimitiveComponent* Prim : OwnedPrimitives)
    {
        if (Prim)
        {
            Prim->SetRenderCustomDepth(false);
            Prim->SetCustomDepthStencilValue(StencilValue);
        }
    }
}
```

> **Dynamic attachments.** Primitives attached to the actor after `BeginPlay` (e.g. equipped weapons, spawned accessories) are not included in `OwnedPrimitives`. If the project attaches geometry dynamically, call a `RefreshPrimitiveCache()` method after attachment, or configure those primitives to inherit CustomDepth from a parent material parameter instead.
> 

---

# Highlight Toggle

```cpp
void UHighlightComponent::SetHighlightActive(bool bActive)
{
    if (bHighlightActive == bActive) return; // No-op

    bHighlightActive = bActive;

    for (UPrimitiveComponent* Prim : OwnedPrimitives)
    {
        if (Prim)
            Prim->SetRenderCustomDepth(bActive);
    }
}
```

---

# Integration with `UInteractionManagerComponent`

`UInteractionManagerComponent` is the **only caller** of `SetHighlightActive`. It queries for `UHighlightComponent` once when the best interactable changes — not on every scan tick.

```cpp
// In UInteractionManagerComponent — called when best component changes:
void UInteractionManagerComponent::OnBestInteractableChanged(
    UInteractionComponent* Previous,
    UInteractionComponent* Next)
{
    // Deactivate highlight on outgoing actor
    if (Previous)
    {
        if (auto* H = Previous->GetOwner()->FindComponentByClass<UHighlightComponent>())
            H->SetHighlightActive(false);
    }

    // Activate highlight on incoming actor
    if (Next)
    {
        if (auto* H = Next->GetOwner()->FindComponentByClass<UHighlightComponent>())
            H->SetHighlightActive(true);
    }
}
```

`FindComponentByClass` is called only on best-change events, which are low-frequency (typically once per second or less). This is not on the hot scan path.

> **`DisablingTags` suppression.** When the scanner clears its current best due to a disabling tag being applied to the pawn, the same teardown path must fire — `SetHighlightActive(false)` on the outgoing actor. Ensure `ClearCurrentBest()` in the manager calls `OnBestInteractableChanged(Previous, nullptr)` consistently.
> 

---

# Post-Process Shader Setup

`UHighlightComponent` drives Custom Depth stencil writes only. The visual outline is rendered by a **post-process material** on the player camera (or a post-process volume). GameCore does not ship a default post-process asset — the project provides its own.

Recommended material logic:

- Sample `SceneTexture:CustomDepth` and `SceneTexture:CustomStencil`.
- Compare stencil value to known categories (1, 2, 3…) to select outline color.
- Use edge-detection (Sobel or depth-difference) on the CustomDepth buffer to draw the outline border.
- Drive outline thickness and color via Material Parameter Collections for designer control.

This approach renders one full-screen pass regardless of how many highlighted actors are on screen — cost is constant, not per-actor.

---

# Performance Notes

- `OwnedPrimitives` is populated once at `BeginPlay`. Zero per-frame cost at steady state.
- `SetRenderCustomDepth` marks the primitive dirty for the next render frame — it does not stall the render thread.
- Custom Depth has a **per-primitive GPU cost** (an additional depth pass). For characters with many skeletal mesh components (armor pieces, accessories), profile early. If cost is excessive, limit `OwnedPrimitives` to a single "LOD root" mesh only.
- The post-process pass is a fixed cost regardless of actor count. Prefer one well-authored PP material over per-material highlight overlays for MMO-scale actor densities.

---

# Known Constraints

- **Dynamically attached primitives** added after `BeginPlay` are not automatically included. Call a cache refresh or handle them separately.
- **HISM instances** are not supported — individual instances inside a `UHierarchicalInstancedStaticMeshComponent` cannot have per-instance Custom Depth. This aligns with the existing HISM limitation noted in the Interaction System Future Work section.
- **Server / non-owning clients** never call `SetHighlightActive`. The component is inert on those machines — Custom Depth state is never replicated.