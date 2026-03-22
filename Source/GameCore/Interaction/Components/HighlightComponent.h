// Copyright GameCore Plugin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HighlightComponent.generated.h"

// Opt-in, client-side presentation component. Added to any actor that should visually
// indicate it is focused by the player's scanner. Fully decoupled from UInteractionComponent.
// Activation driven exclusively by UInteractionManagerComponent on the owning client.
// Inert on the server and non-owning clients.
//
// Implementation uses Custom Depth + Stencil. Zero material permutation cost.
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UHighlightComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	// Stencil value written when highlight is active.
	// Consumed by the post-process outline material to determine color/style.
	// Convention: 1=generic, 2=NPC, 3=item/loot, 4=quest objective.
	// Project-defined — GameCore ships no default post-process asset.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Highlight",
		meta = (ClampMin = "1", ClampMax = "255"))
	uint8 StencilValue = 1;

	// Activate or deactivate the highlight on all owned primitives.
	// Called by UInteractionManagerComponent on best-change. Safe to call with current state.
	UFUNCTION(BlueprintCallable, Category = "Highlight")
	void SetHighlightActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Highlight")
	bool IsHighlightActive() const { return bHighlightActive; }

private:
	virtual void BeginPlay() override;

	// Cached at BeginPlay. Dynamically attached primitives added after BeginPlay not covered.
	TArray<TObjectPtr<UPrimitiveComponent>> OwnedPrimitives;

	bool bHighlightActive = false;
};
