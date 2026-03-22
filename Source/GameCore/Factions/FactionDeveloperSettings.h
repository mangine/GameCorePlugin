// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FactionDeveloperSettings.generated.h"

/**
 * UGameCoreFactionSettings
 *
 * Developer settings for the Faction System.
 * Accessible at Project Settings > GameCore > Factions.
 */
UCLASS(Config = Game, DefaultConfig,
    meta = (DisplayName = "Factions"))
class GAMECORE_API UGameCoreFactionSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UGameCoreFactionSettings()
    {
        CategoryName = TEXT("GameCore");
        SectionName  = TEXT("Factions");
    }

    // The single UFactionRelationshipTable asset for this project.
    // Must be set before entering PIE.
    // UFactionSubsystem logs UE_LOG Fatal if this is null at OnWorldBeginPlay.
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly,
        Category = "Factions",
        meta = (AllowedClasses = "FactionRelationshipTable"))
    FSoftObjectPath FactionRelationshipTable;

    static const UGameCoreFactionSettings* Get()
    {
        return GetDefault<UGameCoreFactionSettings>();
    }
};
