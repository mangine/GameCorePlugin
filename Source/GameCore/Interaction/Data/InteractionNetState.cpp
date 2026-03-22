// Copyright GameCore Plugin. All Rights Reserved.
#include "InteractionNetState.h"
#include "Interaction/Components/InteractionComponent.h"

void FInteractionEntryNetState::PostReplicatedAdd(const FInteractionEntryNetStateArray& Array)
{
	if (Array.OwningComponent)
		Array.OwningComponent->OnEntryStateChanged.Broadcast(Array.OwningComponent, EntryIndex);
}

void FInteractionEntryNetState::PostReplicatedChange(const FInteractionEntryNetStateArray& Array)
{
	if (Array.OwningComponent)
		Array.OwningComponent->OnEntryStateChanged.Broadcast(Array.OwningComponent, EntryIndex);
}

void FInteractionEntryNetState::PreReplicatedRemove(const FInteractionEntryNetStateArray& Array)
{
	if (Array.OwningComponent)
		Array.OwningComponent->OnEntryStateChanged.Broadcast(Array.OwningComponent, EntryIndex);
}
