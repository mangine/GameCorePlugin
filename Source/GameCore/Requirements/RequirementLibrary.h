// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Requirement.h"
#include "RequirementList.h"

// URequirementLibrary is an internal C++ helper for URequirementList.
// It handles the evaluation loop, operator short-circuiting, and dev-time validation.
// Consuming systems must NOT call this directly — always use List->Evaluate(Context).
// This is NOT a UBlueprintFunctionLibrary. It is a plain C++ helper with static methods.
class GAMECORE_API URequirementLibrary
{
public:
	URequirementLibrary() = delete;

	// ── Evaluation ──────────────────────────────────────────────────────────

	// Evaluates all requirements using Evaluate(). Respects Operator short-circuit.
	// AND: first failure wins. OR: first pass wins.
	// Safe on empty arrays — returns Pass immediately.
	static FRequirementResult EvaluateAll(
		const TArray<TObjectPtr<URequirement>>& Requirements,
		ERequirementListOperator Operator,
		const FRequirementContext& Context);

	// Evaluates all requirements using EvaluateFromEvent(). Same short-circuit rules.
	// Called by URequirementList::EvaluateFromEvent.
	static FRequirementResult EvaluateAllFromEvent(
		const TArray<TObjectPtr<URequirement>>& Requirements,
		ERequirementListOperator Operator,
		const FRequirementContext& Context);

	// ── Validation (dev builds only) ─────────────────────────────────────────

	// Validates a requirement array at setup time (BeginPlay or RegisterWatch).
	// Logs errors and returns false if any violations are found.
	//
	// Checks:
	//   - No null entries in the Requirements array.
	//   - NOT composites have exactly one child.
	//   - No null children at any composite nesting depth.
	//   - If ListAuthority is ClientOnly or ClientValidated: logs a warning if
	//     any requirement declares server-only data access (heuristic check).
	//
	// No-op and returns true in Shipping builds.
	static bool ValidateRequirements(
		const TArray<TObjectPtr<URequirement>>& Requirements,
		ERequirementEvalAuthority ListAuthority = ERequirementEvalAuthority::ServerOnly);
};
