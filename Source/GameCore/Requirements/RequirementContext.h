// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "RequirementContext.generated.h"

USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementContext
{
	GENERATED_BODY()

	// The evaluation data. The requirement subclass declares what struct type
	// it expects and casts accordingly. May be empty for requirements that
	// derive all data from world state via subsystem lookup.
	UPROPERTY(BlueprintReadWrite)
	FInstancedStruct Data;

	// Convenience factory. Initialises Data as type T.
	template<typename T>
	static FRequirementContext Make(const T& InData)
	{
		FRequirementContext Ctx;
		Ctx.Data.InitializeAs<T>(InData);
		return Ctx;
	}
};

USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementResult
{
	GENERATED_BODY()

	UPROPERTY() bool bPassed = false;

	// Optional player-facing reason shown in UI when the check fails.
	// Localisation-ready via FText. Empty on Pass results.
	UPROPERTY() FText FailureReason;

	static FRequirementResult Pass()
	{
		return { true, FText::GetEmpty() };
	}

	static FRequirementResult Fail(FText Reason = {})
	{
		return { false, Reason };
	}
};
