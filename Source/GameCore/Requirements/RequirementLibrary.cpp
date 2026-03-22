// Copyright GameCore Plugin. All Rights Reserved.
#include "RequirementLibrary.h"
#include "RequirementComposite.h"

// ── EvaluateAll ────────────────────────────────────────────────────────────

FRequirementResult URequirementLibrary::EvaluateAll(
	const TArray<TObjectPtr<URequirement>>& Requirements,
	ERequirementListOperator Operator,
	const FRequirementContext& Context)
{
	if (Requirements.IsEmpty())
		return FRequirementResult::Pass();

	if (Operator == ERequirementListOperator::AND)
	{
		for (const TObjectPtr<URequirement>& Req : Requirements)
		{
			if (!Req) continue;
			FRequirementResult R = Req->Evaluate(Context);
			if (!R.bPassed) return R; // short-circuit
		}
		return FRequirementResult::Pass();
	}
	else // OR
	{
		FRequirementResult LastFailure = FRequirementResult::Fail();
		for (const TObjectPtr<URequirement>& Req : Requirements)
		{
			if (!Req) continue;
			FRequirementResult R = Req->Evaluate(Context);
			if (R.bPassed) return R; // short-circuit
			LastFailure = R;
		}
		return LastFailure;
	}
}

// ── EvaluateAllFromEvent ───────────────────────────────────────────────────

FRequirementResult URequirementLibrary::EvaluateAllFromEvent(
	const TArray<TObjectPtr<URequirement>>& Requirements,
	ERequirementListOperator Operator,
	const FRequirementContext& Context)
{
	if (Requirements.IsEmpty())
		return FRequirementResult::Pass();

	if (Operator == ERequirementListOperator::AND)
	{
		for (const TObjectPtr<URequirement>& Req : Requirements)
		{
			if (!Req) continue;
			FRequirementResult R = Req->EvaluateFromEvent(Context);
			if (!R.bPassed) return R;
		}
		return FRequirementResult::Pass();
	}
	else
	{
		FRequirementResult LastFailure = FRequirementResult::Fail();
		for (const TObjectPtr<URequirement>& Req : Requirements)
		{
			if (!Req) continue;
			FRequirementResult R = Req->EvaluateFromEvent(Context);
			if (R.bPassed) return R;
			LastFailure = R;
		}
		return LastFailure;
	}
}

// ── ValidateRequirements ───────────────────────────────────────────────────

bool URequirementLibrary::ValidateRequirements(
	const TArray<TObjectPtr<URequirement>>& Requirements,
	ERequirementEvalAuthority ListAuthority)
{
#if UE_BUILD_SHIPPING
	return true;
#else
	bool bValid = true;

	// Iterative traversal using a work stack of (requirement, depth) pairs.
	// The top-level array is the initial work set.
	struct FWorkItem
	{
		const URequirement* Req;
		bool bIsTopLevel;
	};

	TArray<FWorkItem> Stack;
	Stack.Reserve(Requirements.Num());
	for (const TObjectPtr<URequirement>& R : Requirements)
	{
		Stack.Add({ R.Get(), true });
	}

	while (!Stack.IsEmpty())
	{
		FWorkItem Item = Stack.Pop(EAllowShrinking::No);

		// Check for null entry.
		if (!Item.Req)
		{
			UE_LOG(LogTemp, Error,
				TEXT("URequirementLibrary::ValidateRequirements — null entry found in Requirements array."));
			bValid = false;
			continue;
		}

		// Check composite-specific rules.
		if (const URequirement_Composite* Composite = Cast<URequirement_Composite>(Item.Req))
		{
			// NOT composites must have exactly one child.
			if (Composite->Operator == ERequirementOperator::NOT)
			{
				if (Composite->Children.Num() != 1)
				{
					UE_LOG(LogTemp, Error,
						TEXT("URequirementLibrary::ValidateRequirements — NOT composite '%s' has %d children; expected exactly 1."),
						*Composite->GetName(), Composite->Children.Num());
					bValid = false;
				}
			}

			// Recurse into children (null children are caught in next iteration).
			for (const TObjectPtr<URequirement>& Child : Composite->Children)
			{
				Stack.Add({ Child.Get(), false });
			}
		}
	}

	// Authority warning: client-side lists should not contain requirements that
	// would typically require server-only data. This is a heuristic warning only.
	if (ListAuthority == ERequirementEvalAuthority::ClientOnly ||
		ListAuthority == ERequirementEvalAuthority::ClientValidated)
	{
		// Currently a placeholder heuristic — concrete requirement subclasses
		// can opt-in to a warning by overriding a (future) IsServerOnly() virtual.
		// For now, emit a general advisory.
		UE_LOG(LogTemp, Verbose,
			TEXT("URequirementLibrary::ValidateRequirements — list uses client-side authority. "
				 "Ensure no requirements access server-only data."));
	}

	return bValid;
#endif // UE_BUILD_SHIPPING
}
