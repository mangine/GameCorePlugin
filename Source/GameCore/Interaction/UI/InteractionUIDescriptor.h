// Copyright GameCore Plugin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interaction/ResolvedInteractionOption.h"
#include "InteractionUIDescriptor.generated.h"

class UInteractionContextWidget;

// Abstract base. Subclass in Blueprint or C++ per interaction category.
// Instances are STATELESS — do not store per-actor data here.
// All actor-contextual data must be read from Interactable at call time.
//
// UInteractionDescriptorSubsystem caches exactly one instance per class.
// The same instance is shared across all actors using that entry config.
UCLASS(Abstract, Blueprintable, BlueprintType)
class GAMECORE_API UInteractionUIDescriptor : public UObject
{
	GENERATED_BODY()

public:
	// Called by the interaction widget when a resolved option is focused.
	// Populate Widget with data from Option (static config) and Interactable (live actor).
	//
	// Interactable may be null — actor may be destroyed between resolve and display.
	// Always null-check before reading from it.
	UFUNCTION(BlueprintNativeEvent, Category = "Interaction|UI")
	void PopulateContextWidget(
		UInteractionContextWidget* Widget,
		const FResolvedInteractionOption& Option,
		AActor* Interactable) const;

	virtual void PopulateContextWidget_Implementation(
		UInteractionContextWidget* Widget,
		const FResolvedInteractionOption& Option,
		AActor* Interactable) const {}
};
