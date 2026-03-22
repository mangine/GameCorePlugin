// Copyright GameCore Plugin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Interaction/Data/InteractionEntryConfig.h"
#include "InteractionEntryDataAsset.generated.h"

// Reusable, named interaction entry. One asset referenced by N UInteractionComponents.
// ShowOnlyInnerProperties flattens Config in the Details panel — designers see all
// fields directly without expanding a nested struct.
UCLASS(Blueprintable, BlueprintType)
class GAMECORE_API UInteractionEntryDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ShowOnlyInnerProperties))
	FInteractionEntryConfig Config;
};
