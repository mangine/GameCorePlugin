# `FInteractionEntryConfig` and `UInteractionEntryDataAsset`

**Files:** `Interaction/Data/InteractionEntryConfig.h` | `Interaction/Data/InteractionEntryDataAsset.h/.cpp`

`FInteractionEntryConfig` is the **complete, immutable definition** of a single interaction entry. It lives identically on server and all clients — never replicated. Must never be modified at runtime.

`UInteractionEntryDataAsset` wraps a single `FInteractionEntryConfig` in a named `UDataAsset`, allowing designers to create reusable entry definitions in the Content Browser and share them across many `UInteractionComponent` instances.

---

## `FInteractionEntryConfig`

```cpp
// File: Interaction/Data/InteractionEntryConfig.h
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagQuery.h"
#include "Interaction/Enums/InteractionEnums.h"
#include "InteractionEntryConfig.generated.h"

class URequirementList;
class UInputAction;
class UTexture2D;
class UInteractionUIDescriptor;

USTRUCT(BlueprintType)
struct GAMECORE_API FInteractionEntryConfig
{
    GENERATED_BODY()

    // ── Tag Filters ───────────────────────────────────────────────────────────

    // Tags the SOURCE actor (player pawn) must ALL have.
    // Evaluated client-side (ResolveOptions) and server-side (validation).
    // Bitset AND — near-zero cost. Prefer over SourceTagQuery for simple gates.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Availability")
    FGameplayTagContainer SourceRequiredTags;

    // Tags the TARGET actor (this interactable) must ALL have.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Availability")
    FGameplayTagContainer TargetRequiredTags;

    // Advanced AND/OR/NOT query on the source actor.
    // Evaluated only if non-empty. More expensive than tag containers.
    // Applied after SourceRequiredTags if both are set — both must pass.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Availability|Advanced")
    FGameplayTagQuery SourceTagQuery;

    // Advanced AND/OR/NOT query on the target actor.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Availability|Advanced")
    FGameplayTagQuery TargetTagQuery;

    // ── Requirements ─────────────────────────────────────────────────────────

    // Optional requirement list for this entry.
    // Authority must be ClientValidated — evaluated client-side for Locked display,
    // then re-evaluated server-side authoritatively during ServerRequestInteract.
    //
    // Requirements in the list must be synchronous (IsAsync() == false).
    // Detected at BeginPlay via URequirementLibrary::ValidateRequirements.
    //
    // FRequirementContext available during evaluation:
    //   Context.Instigator  = the interacting pawn (source)
    //   Context.PlayerState = the pawn's PlayerState
    //   Context.World       = GetWorld()
    //
    // When null, this validation step is skipped entirely — no overhead.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Availability")
    TObjectPtr<URequirementList> EntryRequirements;

    // ── Grouping & Priority ───────────────────────────────────────────────────

    // The interaction group this entry competes within.
    // In EResolveMode::Best, only the highest-priority entry per group is shown.
    // Example groups: Interaction.Group.Primary, Interaction.Group.Secondary
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority")
    FGameplayTag InteractionGroupTag;

    // Tiebreaker within the group. Higher value wins.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority")
    int32 OptionPriority = 100;

    // If true and this is the highest-priority Available exclusive candidate,
    // it becomes the ONLY resolved option — all others are suppressed.
    // Use for interactions that demand undivided attention (cutscene trigger,
    // critical quest event, boarding a ship).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority")
    bool bExclusive = false;

    // ── Input & UI ────────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    EInteractionInputType InputType = EInteractionInputType::Press;

    // Required hold duration in seconds. Ignored when InputType == Press.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction",
        meta = (EditCondition = "InputType == EInteractionInputType::Hold", ClampMin = "0.1"))
    float HoldTimeSeconds = 1.5f;

    // Localizable action verb shown in the interaction prompt.
    // Accessed by pointer in FResolvedInteractionOption — never copied.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    FText Label;

    // Enhanced Input action. UI resolves correct key icon and respects remapping.
    // Soft reference — loads only when a prompt widget needs to display.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    TSoftObjectPtr<UInputAction> InputAction;

    // Optional per-entry icon. Bypasses the component's IconDataAsset state mapping.
    // Use for entries with unique iconography (faction crest, quest marker).
    // Also used as the Locked-state icon when EntryRequirements fails.
    // If null, icon resolution falls through to the component's IconDataAsset.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    TSoftObjectPtr<UTexture2D> EntryIconOverride;

    // Optional descriptor class for contextual UI population.
    // One shared instance per class is cached by UInteractionDescriptorSubsystem.
    // Null is valid — the context panel stays hidden.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI")
    TSubclassOf<UInteractionUIDescriptor> UIDescriptorClass;
};
```

### Notes

- **`EntryRequirements` uses `URequirementList`** (not a raw `TArray<URequirement*>`). This gives the list its own authority declaration (`ClientValidated`) and operator configuration. The list is a DataAsset — it can be shared across multiple entries.
- **`EntryRequirements` must have `Authority = ClientValidated`**. The Interaction System evaluates it client-side for display (Locked state) and server-side for authority. A `ServerOnly` list would never gate the client display correctly.
- **No per-condition icon override.** Use `EntryIconOverride` on the config for a static locked-state icon. Dynamic per-condition icons should be handled in the widget based on `ConditionLabel` content.
- **Target actor is not in `FRequirementContext`.** Tag-based conditions on the target should use `TargetRequiredTags` / `TargetTagQuery` (the fast path). If a requirement genuinely needs the target actor, retrieve it via the pawn's targeting component — never via a hard dependency on the interactable's type.

---

## `UInteractionEntryDataAsset`

```cpp
// File: Interaction/Data/InteractionEntryDataAsset.h
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Interaction/Data/InteractionEntryConfig.h"
#include "InteractionEntryDataAsset.generated.h"

// Reusable, named interaction entry. One asset referenced by N UInteractionComponents.
// ShowOnlyInnerProperties flattens Config in the Details panel — designers see all
// fields directly without expanding a nested struct.
UCLASS(Blueprintable, BlueprintType)
class GAMECORE_API UInteractionEntryDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ShowOnlyInnerProperties))
    FInteractionEntryConfig Config;
};
```

### Why DataAsset over EditInlineNew

`EditInlineNew` creates one UObject instance per component per entry — a Shop NPC and a Quest NPC each hold their own separate object with identical data. A DataAsset is one asset referenced by N components. For an MMORPG with hundreds of actors sharing common entry types (Shop, Talk, Quest, Harvest), DataAssets eliminate that duplication entirely — one GC object, not one per actor.
