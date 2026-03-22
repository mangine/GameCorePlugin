// Copyright GameCore Plugin. All Rights Reserved.
#include "InteractionDescriptorSubsystem.h"
#include "InteractionUIDescriptor.h"

UInteractionUIDescriptor* UInteractionDescriptorSubsystem::GetOrCreate(
	TSubclassOf<UInteractionUIDescriptor> Class)
{
	if (!Class) return nullptr;

	if (TObjectPtr<UInteractionUIDescriptor>* Found = Cache.Find(Class))
		return *Found;

	UInteractionUIDescriptor* New = NewObject<UInteractionUIDescriptor>(this, Class);
	Cache.Add(Class, New);
	return New;
}

void UInteractionDescriptorSubsystem::Invalidate(TSubclassOf<UInteractionUIDescriptor> Class)
{
	Cache.Remove(Class);
}

void UInteractionDescriptorSubsystem::ClearAll()
{
	Cache.Empty();
}

void UInteractionDescriptorSubsystem::Deinitialize()
{
	ClearAll();
	Super::Deinitialize();
}
