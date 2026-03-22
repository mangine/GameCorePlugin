// Copyright GameCore Plugin. All Rights Reserved.
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
