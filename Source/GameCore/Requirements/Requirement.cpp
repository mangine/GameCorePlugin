// Copyright GameCore Plugin. All Rights Reserved.
#include "Requirement.h"

#define LOCTEXT_NAMESPACE "RequirementSystem"

FRequirementResult URequirement::Evaluate(const FRequirementContext& Context) const
{
	return FRequirementResult::Fail(
		LOCTEXT("NotImplemented", "Requirement does not support imperative evaluation."));
}

FRequirementResult URequirement::EvaluateFromEvent(const FRequirementContext& Context) const
{
	// Default: reuse imperative path.
	return Evaluate(Context);
}

#if WITH_EDITOR
FString URequirement::GetDescription() const
{
	return GetClass()->GetDisplayNameText().ToString();
}
#endif

#undef LOCTEXT_NAMESPACE
