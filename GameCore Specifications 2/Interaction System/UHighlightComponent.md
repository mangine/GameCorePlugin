# `UHighlightComponent`

**File:** `Interaction/Components/HighlightComponent.h/.cpp`

Opt-in, client-side presentation component. Added to any actor that should visually indicate it is focused by the player's scanner. Fully decoupled from `UInteractionComponent` — an actor may have both, but neither requires the other.

Activation is driven exclusively by `UInteractionManagerComponent` on the owning client, via the scan's best-change logic. **Inert on the server and non-owning clients.**

---

## Design Rationale

Highlighting is presentation, not interaction logic. Keeping it separate:
- Preserves `UInteractionComponent` as a pure data/state container.
- Allows highlighting non-interactable actors (quest targets, hostile NPCs) without touching the interaction system.
- Stencil value is configurable per-actor type — the post-process shader can color-code categories independently.

Implementation uses **Custom Depth + Stencil**. Zero material permutation cost, works on Skeletal and Static meshes, toggle is a single `SetRenderCustomDepth` call per primitive.

---

## Class Definition

```cpp
#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HighlightComponent.generated.h"

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UHighlightComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    // Stencil value written when highlight is active.
    // Consumed by the post-process outline material to determine color/style.
    // Convention: 1=generic, 2=NPC, 3=item/loot, 4=quest objective.
    // Project-defined — GameCore ships no default post-process asset.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Highlight",
        meta = (ClampMin = "1", ClampMax = "255"))
    uint8 StencilValue = 1;

    // Activate or deactivate the highlight on all owned primitives.
    // Called by UInteractionManagerComponent on best-change. Safe to call with current state.
    UFUNCTION(BlueprintCallable, Category = "Highlight")
    void SetHighlightActive(bool bActive);

    UFUNCTION(BlueprintPure, Category = "Highlight")
    bool IsHighlightActive() const { return bHighlightActive; }

private:
    virtual void BeginPlay() override;

    // Cached at BeginPlay. Dynamically attached primitives added after BeginPlay not covered.
    TArray<TObjectPtr<UPrimitiveComponent>> OwnedPrimitives;

    bool bHighlightActive = false;
};
```

---

## Implementation

```cpp
void UHighlightComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner) return;

    Owner->GetComponents<UPrimitiveComponent>(OwnedPrimitives);

    for (UPrimitiveComponent* Prim : OwnedPrimitives)
    {
        if (Prim)
        {
            Prim->SetRenderCustomDepth(false);
            Prim->SetCustomDepthStencilValue(StencilValue);
        }
    }
}

void UHighlightComponent::SetHighlightActive(bool bActive)
{
    if (bHighlightActive == bActive) return; // No-op

    bHighlightActive = bActive;
    for (UPrimitiveComponent* Prim : OwnedPrimitives)
        if (Prim) Prim->SetRenderCustomDepth(bActive);
}
```

---

## Post-Process Shader Notes

`UHighlightComponent` writes Custom Depth stencil values only. The visual outline is rendered by a **post-process material** on the player camera or a PP volume. GameCore does not ship a default PP asset — the project provides its own.

Recommended material logic:
- Sample `SceneTexture:CustomDepth` and `SceneTexture:CustomStencil`.
- Compare stencil value to known categories (1, 2, 3…) to select outline color.
- Use edge-detection (Sobel or depth-difference) on the CustomDepth buffer.
- Drive thickness and color via Material Parameter Collections.

One full-screen pass regardless of highlighted actor count — cost is constant, not per-actor.

---

## Performance Notes

- `OwnedPrimitives` populated once at `BeginPlay`. Zero per-frame cost at steady state.
- `SetRenderCustomDepth` marks the primitive dirty for the next render frame — no render thread stall.
- Custom Depth has a per-primitive GPU cost (additional depth pass). For characters with many mesh components, profile early. Consider limiting to a single LOD root mesh.

---

## Constraints

- **Dynamically attached primitives** added after `BeginPlay` not included. Call a cache refresh or handle separately.
- **HISM instances not supported.** Per-instance Custom Depth is not possible on `UHierarchicalInstancedStaticMeshComponent`. Aligns with the HISM limitation in the interaction system.
- **Server / non-owning clients** never call `SetHighlightActive`. Custom Depth state is never replicated.
