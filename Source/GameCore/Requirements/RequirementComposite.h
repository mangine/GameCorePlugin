// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Requirement.h"
#include "RequirementComposite.generated.h"

UENUM(BlueprintType)
enum class ERequirementOperator : uint8
{
	// All children must pass. Short-circuits on first failure.
	AND UMETA(DisplayName = "AND — All Must Pass"),

	// Any child passing is sufficient. Short-circuits on first pass.
	OR  UMETA(DisplayName = "OR — Any Must Pass"),

	// Exactly one child. Result is inverted.
	// Child passes → Composite fails (using NotFailureReason).
	// Child fails  → Composite passes.
	NOT UMETA(DisplayName = "NOT — Must Not Pass"),
};

UCLASS(DisplayName = "Composite (AND / OR / NOT)", EditInlineNew, CollapseCategories)
class GAMECORE_API URequirement_Composite : public URequirement
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Composite")
	ERequirementOperator Operator = ERequirementOperator::AND;

	// Child requirements. Each may itself be a URequirement_Composite.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Composite")
	TArray<TObjectPtr<URequirement>> Children;

	// Failure reason used when NOT fails (i.e. the child passed).
	// The child's own FailureReason is semantically wrong here — the child passed.
	// Ignored when Operator != NOT.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Composite",
		meta = (EditCondition = "Operator == ERequirementOperator::NOT"))
	FText NotFailureReason;

	virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override;
	virtual FRequirementResult EvaluateFromEvent(const FRequirementContext& Context) const override;

#if WITH_EDITOR
	virtual FString GetDescription() const override;
#endif
};
