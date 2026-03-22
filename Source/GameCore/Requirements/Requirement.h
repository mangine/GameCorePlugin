// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "RequirementContext.h"
#include "Requirement.generated.h"

UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API URequirement : public UObject
{
	GENERATED_BODY()
public:

	// ── Imperative Evaluation ──────────────────────────────────────────────

	// Called when the consuming system needs to check this condition right now,
	// with an explicitly-built context.
	//
	// The subclass casts Context.Data to its expected struct type and evaluates.
	// If Context.Data is empty or the wrong type, return Fail with a clear reason.
	//
	// Default: returns Fail with a "not implemented" message.
	// Requirements that are purely event-driven keep this default.
	//
	// Must be const and stateless — no mutation of any instance variable.
	virtual FRequirementResult Evaluate(const FRequirementContext& Context) const;

	// ── Event-Driven Evaluation ────────────────────────────────────────────

	// Called by the RegisterWatch closure when a subscribed event arrives.
	// Context.Data contains the event payload (FInstancedStruct from the event bus).
	//
	// Default: delegates to Evaluate(Context).
	// Override only when event-specific behaviour differs from snapshot evaluation.
	virtual FRequirementResult EvaluateFromEvent(const FRequirementContext& Context) const;

	// ── Watcher Registration ───────────────────────────────────────────────

	// Returns the set of RequirementEvent.* tags that invalidate this requirement.
	// Called once by URequirementList::CollectWatchedEvents at RegisterWatch time.
	// Never called per-frame.
	//
	// Return empty if this requirement is never used in a watched list.
	// Tags must be in the RequirementEvent.* namespace.
	UFUNCTION(BlueprintNativeEvent, Category = "Requirement")
	void GetWatchedEvents(FGameplayTagContainer& OutEvents) const;
	virtual void GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const {}

	// ── Editor ────────────────────────────────────────────────────────────

#if WITH_EDITOR
	// Human-readable description including configured property values.
	// Shown in the Details panel and composite tooltips.
	// Example: "MinLevel >= 20", "Has Tag: Status.QuestReady"
	// Default: returns class DisplayName.
	virtual FString GetDescription() const;
#endif
};
