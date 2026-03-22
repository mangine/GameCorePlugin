// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ILootRewardable.generated.h"

/**
 * Marker interface. Any asset that can appear as a RewardDefinition in a
 * FLootTableEntry must implement this interface. The interface carries no
 * methods — its presence on a class is the sole contract.
 *
 * ILootRewardable lives in the runtime GameCore module.
 * Implementing it requires no editor dependency.
 *
 * Example:
 *   UCLASS()
 *   class UItemDefinition : public UPrimaryDataAsset, public ILootRewardable
 *   { GENERATED_BODY() };
 *
 * Editor filtering (FFLootEntryRewardCustomization) is enforced at authoring time only.
 * Not enforced at runtime — fulfillment layer is responsible for safe casting.
 */
UIINTERFACE(MinimalAPI, NotBlueprintable)
class ULootRewardable : public UInterface
{
    GENERATED_BODY()
};

class GAMECORE_API ILootRewardable
{
    GENERATED_BODY()
    // Intentionally empty — presence is the sole contract.
};
