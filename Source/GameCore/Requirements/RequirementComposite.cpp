// Copyright GameCore Plugin. All Rights Reserved.
#include "RequirementComposite.h"

#define LOCTEXT_NAMESPACE "RequirementSystem"

FRequirementResult URequirement_Composite::Evaluate(const FRequirementContext& Context) const
{
	switch (Operator)
	{
	case ERequirementOperator::AND:
	{
		for (const TObjectPtr<URequirement>& Child : Children)
		{
			if (!Child) continue;
			FRequirementResult R = Child->Evaluate(Context);
			if (!R.bPassed) return R; // short-circuit: first failure wins
		}
		return FRequirementResult::Pass();
	}
	case ERequirementOperator::OR:
	{
		FRequirementResult LastFailure = FRequirementResult::Fail();
		for (const TObjectPtr<URequirement>& Child : Children)
		{
			if (!Child) continue;
			FRequirementResult R = Child->Evaluate(Context);
			if (R.bPassed) return R; // short-circuit: first pass wins
			LastFailure = R;
		}
		return LastFailure;
	}
	case ERequirementOperator::NOT:
	{
		if (Children.IsEmpty() || !Children[0])
			return FRequirementResult::Fail(
				LOCTEXT("NOT_NoChild", "NOT composite has no child."));
		FRequirementResult R = Children[0]->Evaluate(Context);
		return R.bPassed
			? FRequirementResult::Fail(NotFailureReason) // child passed → NOT fails
			: FRequirementResult::Pass();                // child failed → NOT passes
	}
	}
	return FRequirementResult::Fail();
}

FRequirementResult URequirement_Composite::EvaluateFromEvent(
	const FRequirementContext& Context) const
{
	// Mirror of Evaluate but calls EvaluateFromEvent on children,
	// allowing event-only children to participate correctly.
	switch (Operator)
	{
	case ERequirementOperator::AND:
	{
		for (const TObjectPtr<URequirement>& Child : Children)
		{
			if (!Child) continue;
			FRequirementResult R = Child->EvaluateFromEvent(Context);
			if (!R.bPassed) return R;
		}
		return FRequirementResult::Pass();
	}
	case ERequirementOperator::OR:
	{
		FRequirementResult LastFailure = FRequirementResult::Fail();
		for (const TObjectPtr<URequirement>& Child : Children)
		{
			if (!Child) continue;
			FRequirementResult R = Child->EvaluateFromEvent(Context);
			if (R.bPassed) return R;
			LastFailure = R;
		}
		return LastFailure;
	}
	case ERequirementOperator::NOT:
	{
		if (Children.IsEmpty() || !Children[0])
			return FRequirementResult::Fail(
				LOCTEXT("NOT_NoChild", "NOT composite has no child."));
		FRequirementResult R = Children[0]->EvaluateFromEvent(Context);
		return R.bPassed
			? FRequirementResult::Fail(NotFailureReason)
			: FRequirementResult::Pass();
	}
	}
	return FRequirementResult::Fail();
}

#if WITH_EDITOR
FString URequirement_Composite::GetDescription() const
{
	FString OpStr;
	switch (Operator)
	{
	case ERequirementOperator::AND: OpStr = TEXT("AND"); break;
	case ERequirementOperator::OR:  OpStr = TEXT("OR");  break;
	case ERequirementOperator::NOT: OpStr = TEXT("NOT"); break;
	}
	return FString::Printf(TEXT("Composite [%s] (%d children)"), *OpStr, Children.Num());
}
#endif

#undef LOCTEXT_NAMESPACE
