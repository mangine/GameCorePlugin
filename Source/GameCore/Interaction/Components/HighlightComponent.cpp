// Copyright GameCore Plugin. All Rights Reserved.
#include "HighlightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

void UHighlightComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	Owner->GetComponents<UPrimitiveComponent>(OwnedPrimitives);

	for (UPrimitiveComponent* Prim : OwnedPrimitives)
	{
		if (Prim)
		{
			Prim->SetRenderCustomDepth(false);
			Prim->SetCustomDepthStencilValue(StencilValue);
		}
	}
}

void UHighlightComponent::SetHighlightActive(bool bActive)
{
	if (bHighlightActive == bActive) return; // No-op

	bHighlightActive = bActive;
	for (UPrimitiveComponent* Prim : OwnedPrimitives)
		if (Prim) Prim->SetRenderCustomDepth(bActive);
}
