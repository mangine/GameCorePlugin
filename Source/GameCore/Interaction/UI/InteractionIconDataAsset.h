// Copyright GameCore Plugin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Interaction/Enums/InteractionEnums.h"
#include "InteractionIconDataAsset.generated.h"

// Maps EInteractableState to display icons.
// All references are soft — icons load on demand. Asset itself is lightweight at rest.
UCLASS(BlueprintType)
class GAMECORE_API UInteractionIconDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
	TSoftObjectPtr<UTexture2D> AvailableIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
	TSoftObjectPtr<UTexture2D> OccupiedIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
	TSoftObjectPtr<UTexture2D> CooldownIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icons")
	TSoftObjectPtr<UTexture2D> LockedIcon;

	// Disabled entries are never in FResolvedInteractionOption — no icon field needed.

	// Returns the soft pointer for the given state.
	// Returns empty for Disabled or unknown states.
	// Result is a soft pointer — caller must async load before display.
	UFUNCTION(BlueprintCallable, Category = "Icons")
	TSoftObjectPtr<UTexture2D> GetIconForState(EInteractableState State) const;
};
