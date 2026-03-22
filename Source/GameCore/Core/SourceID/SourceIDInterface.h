// SourceIDInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "SourceIDInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class USourceIDInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * Implemented by any UObject that can identify itself as an event source.
 * Used by logging, audit trails, and analytics across multiple systems
 * (XP grants, item drops, market events, etc.).
 *
 * Tag convention: Source.<Category>.<SubType>.<Detail>
 * Examples:
 *   Source.Mob.Skeleton.Level10
 *   Source.Quest.MainStory.Act1
 *   Source.Market.PlayerTrade
 *   Source.Event.SeasonalEvent.WinterFest
 */
class GAMECORE_API ISourceIDInterface
{
    GENERATED_BODY()

public:
    /**
     * Returns a structured gameplay tag identifying this source.
     * Must be overridden. Tag should follow the Source.* hierarchy.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Source ID")
    FGameplayTag GetSourceTag() const;
    virtual FGameplayTag GetSourceTag_Implementation() const = 0;

    /**
     * Optional human-readable name for CS tooling and debug logs.
     * Returns empty text by default; override only when tooling requires it.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Source ID")
    FText GetSourceDisplayName() const;
    virtual FText GetSourceDisplayName_Implementation() const
    {
        return FText::GetEmpty();
    }

    /**
     * Returns the stable persistent identity GUID for this actor.
     * Required by the Serialization System (UPersistenceRegistrationComponent).
     * Must be unique per entity and stable across sessions.
     * Generate once at spawn and persist in the actor's save data.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Source ID")
    FGuid GetEntityGUID() const;
    virtual FGuid GetEntityGUID_Implementation() const
    {
        return FGuid();
    }
};
