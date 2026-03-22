// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Requirement.h"
#include "RequirementList.generated.h"

// Forward declaration — full type needed only in .cpp where RegisterWatch is implemented.
class UGameCoreEventWatcher;
struct FEventWatchHandle;
enum class EGameCoreEventScope : uint8;

UENUM(BlueprintType)
enum class ERequirementEvalAuthority : uint8
{
	// Server evaluates only. Use for all gameplay-gating checks.
	// Client never receives a result until the server decides.
	ServerOnly       UMETA(DisplayName = "Server Only"),

	// Client evaluates only. Server never checks.
	// Use for cosmetic / UI gating where authoritative evaluation is not needed.
	ClientOnly       UMETA(DisplayName = "Client Only"),

	// Client evaluates for responsiveness. On all-pass, fires a Server RPC.
	// Server re-evaluates fully from its own context — never trusts the client result.
	// Use for player-facing unlocks where immediate UI feedback matters.
	ClientValidated  UMETA(DisplayName = "Client Validated"),
};

UENUM(BlueprintType)
enum class ERequirementListOperator : uint8
{
	// All requirements must pass. Short-circuits on first failure.
	AND UMETA(DisplayName = "All Must Pass (AND)"),

	// Any single requirement passing is sufficient. Short-circuits on first pass.
	OR  UMETA(DisplayName = "Any Must Pass (OR)"),
};

UCLASS(BlueprintType, DisplayName = "Requirement List")
class GAMECORE_API URequirementList : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:

	// ── Authoring ──────────────────────────────────────────────────────────

	// Top-level evaluation operator.
	// AND: all requirements must pass. Short-circuits on first failure.
	// OR:  any requirement passing is sufficient. Short-circuits on first pass.
	// Place cheap checks first.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements")
	ERequirementListOperator Operator = ERequirementListOperator::AND;

	// Requirements evaluated by this list.
	// URequirement_Composite is valid here for nested AND/OR/NOT logic.
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Requirements")
	TArray<TObjectPtr<URequirement>> Requirements;

	// Network authority. Set once by the designer. Never overridden at call sites.
	UPROPERTY(EditDefaultsOnly, Category = "Network")
	ERequirementEvalAuthority Authority = ERequirementEvalAuthority::ServerOnly;

	// ── Imperative Evaluation ──────────────────────────────────────────────

	// Evaluates all requirements synchronously. Always callable without prior registration.
	UFUNCTION(BlueprintCallable, Category = "Requirements")
	FRequirementResult Evaluate(const FRequirementContext& Context) const;

	// Evaluates all requirements using the event path.
	// Called internally by the RegisterWatch closure when an event fires.
	FRequirementResult EvaluateFromEvent(const FRequirementContext& Context) const;

	// ── Reactive Watch Registration ────────────────────────────────────────

	// Registers this list for reactive evaluation.
	//
	// Collects watched tags from all requirements via GetWatchedEvents.
	// Registers a closure with UGameCoreEventWatcher that:
	//   1. Wraps the event payload in FRequirementContext.
	//   2. Calls EvaluateFromEvent.
	//   3. Calls OnResult(bPassed) only if the pass/fail state changed.
	//
	// Authority is read from this->Authority and mapped to EGameCoreEventScope.
	// The watcher skips the callback silently if the net role does not match.
	//
	// OnResult captures caller context via closure — this list never inspects it.
	// Use TWeakObjectPtr for any UObject captured in OnResult.
	//
	// Returns one FEventWatchHandle covering all tag subscriptions.
	// Pass to UnregisterWatch at teardown.
	// Returns an invalid handle if the list has no watched events.
	FEventWatchHandle RegisterWatch(
		const UObject* Owner,
		TFunction<void(bool /*bPassed*/)> OnResult) const;

	// Removes all subscriptions established by RegisterWatch.
	// Safe to call with an invalid handle.
	static void UnregisterWatch(
		const UObject* Owner,
		FEventWatchHandle Handle);

	// ── Internal Utilities ─────────────────────────────────────────────────

	// Collects all RequirementEvent.* tags from every requirement (composite children
	// included). Called once per RegisterWatch call.
	void CollectWatchedEvents(FGameplayTagContainer& OutTags) const;

	// Returns flat list of all requirements (composite children included).
	// Used by URequirementLibrary::ValidateRequirements.
	TArray<URequirement*> GetAllRequirements() const;

private:
	// Maps ERequirementEvalAuthority to EGameCoreEventScope for watcher registration.
	EGameCoreEventScope AuthorityToScope() const;
};
