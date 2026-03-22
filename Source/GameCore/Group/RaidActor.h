// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RaidActor.generated.h"

class URaidComponent;

/**
 * ARaidActor
 *
 * Server-only container for a multi-group raid.
 * Holds references to constituent AGroupActor instances via URaidComponent.
 *
 * bReplicates = false — clients never see this actor.
 */
UCLASS(NotBlueprintable)
class GAMECORE_API ARaidActor : public AActor
{
    GENERATED_BODY()
public:
    ARaidActor();

    URaidComponent* GetRaidComponent() const { return RaidComponent; }

private:
    UPROPERTY()
    TObjectPtr<URaidComponent> RaidComponent;
};
