// Copyright GameCore Plugin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Interaction/Enums/InteractionEnums.h"
#include "ResolvedInteractionOption.generated.h"

class UInteractionComponent;
class UInputAction;
class UTexture2D;
class UInteractionUIDescriptor;

// Client-side resolution output. Produced by UInteractionComponent::ResolveOptions().
// Consumed exclusively by the UI layer. Never replicated.
// Must not be cached across frames — the owning buffer is reset on every re-resolve.
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
