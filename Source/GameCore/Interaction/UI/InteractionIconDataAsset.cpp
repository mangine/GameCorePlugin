// Copyright GameCore Plugin. All Rights Reserved.
#include "InteractionIconDataAsset.h"

TSoftObjectPtr<UTexture2D> UInteractionIconDataAsset::GetIconForState(EInteractableState State) const
{
	switch (State)
	{
		case EInteractableState::Available: return AvailableIcon;
		case EInteractableState::Occupied:  return OccupiedIcon;
		case EInteractableState::Cooldown:  return CooldownIcon;
		case EInteractableState::Locked:    return LockedIcon;
		default:                            return {};
	}
}
