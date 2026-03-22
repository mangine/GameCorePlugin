// Copyright GameCore Plugin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "InteractionDescriptorSubsystem.generated.h"

class UInteractionUIDescriptor;

// Caches one UInteractionUIDescriptor instance per class for the game session.
// Sole allocation site for descriptor objects — nothing else calls NewObject<UInteractionUIDescriptor>.
//
// Memory profile:
//   300 NPCs using UNPCDescriptor    → 1 instance
//   200 chests using UChestDescriptor → 1 instance
//   N actors, K distinct descriptor classes → K instances
UCLASS()
class GAMECORE_API UInteractionDescriptorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Returns the shared descriptor instance for Class.
	// Creates and caches it on first call. Returns null if Class is null.
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	UInteractionUIDescriptor* GetOrCreate(TSubclassOf<UInteractionUIDescriptor> Class);

	// Removes a cached descriptor, forcing re-creation on next GetOrCreate.
	// For hot-reload and editor utility use only.
	void Invalidate(TSubclassOf<UInteractionUIDescriptor> Class);

	void ClearAll();

protected:
	virtual void Deinitialize() override;

private:
	// One entry per class. Outer is this subsystem (GC root).
	UPROPERTY()
	TMap<TSubclassOf<UInteractionUIDescriptor>, TObjectPtr<UInteractionUIDescriptor>> Cache;
};
