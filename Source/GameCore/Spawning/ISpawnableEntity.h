#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISpawnableEntity.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class USpawnableEntity : public UInterface
{
    GENERATED_BODY()
};

/**
 * Marker interface. Any actor class that should appear in the FSpawnEntry asset picker
 * must implement this interface. The single BlueprintNativeEvent has a no-op C++ default
 * and serves purely as a post-spawn notification hook.
 */
class GAMECORE_API ISpawnableEntity
{
    GENERATED_BODY()
public:
    /**
     * Called by USpawnManagerComponent immediately after the actor finishes spawning
     * (post FinishSpawning) and before it is added to LiveInstances tracking.
     *
     * Use this to receive context from the spawner:
     *   - Read the loot table override via Mgr->GetLootTableOverrideForClass
     *   - Set AI parameters / faction / difficulty tier
     *
     * Anchor is the USpawnManagerComponent's owner actor.
     * Default C++ implementation is a no-op — overriding is not required.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Spawning")
    void OnSpawnedByManager(AActor* Anchor);
    virtual void OnSpawnedByManager_Implementation(AActor* Anchor) {}
};
