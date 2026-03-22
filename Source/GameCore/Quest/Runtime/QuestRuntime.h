#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameFramework/PlayerState.h"
#include "QuestRuntime.generated.h"

/**
 * Snapshot struct injected into FRequirementContext::Data before requirement evaluation.
 * Quest-owned requirements cast Context.Data to FQuestEvaluationContext* and retrieve
 * UQuestComponent from PlayerState via FindComponentByClass.
 */
USTRUCT()
struct GAMECORE_API FQuestEvaluationContext
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<APlayerState> PlayerState = nullptr;

    UPROPERTY()
    TObjectPtr<UWorld> World = nullptr;
};

// Forward declaration for FFastArraySerializer callbacks.
struct FQuestRuntimeArray;

/**
 * Per-player instance of one active quest.
 * Element of UQuestComponent::ActiveQuests (FFastArraySerializer for delta replication).
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FQuestTrackerEntry
{
    GENERATED_BODY()

    /** Matches FQuestProgressTrackerDef::TrackerKey in the stage definition. */
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag TrackerKey;

    /** Current accumulated value. Always >= 0, clamped to EffectiveTarget. */
    UPROPERTY(BlueprintReadOnly)
    int32 CurrentValue = 0;

    /**
     * Effective target for this player/group configuration.
     * Stored so the client can display progress without loading the definition asset.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 EffectiveTarget = 1;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestRuntime : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag QuestId;

    /** Current stage. Matches a state tag in UQuestDefinition::StageGraph. */
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag CurrentStageTag;

    /** Progress counters for the current stage. Only entries with bReEvaluateOnly=false are present. */
    UPROPERTY(BlueprintReadOnly)
    TArray<FQuestTrackerEntry> Trackers;

    /**
     * Unix timestamp (seconds) of last completion.
     * Used by URequirement_QuestCooldown. 0 = never completed.
     */
    UPROPERTY(BlueprintReadOnly)
    int64 LastCompletedTimestamp = 0;

    // FFastArraySerializer callbacks — called on the receiving (client) side.
    void PostReplicatedAdd(const FQuestRuntimeArray& Array);
    void PostReplicatedChange(const FQuestRuntimeArray& Array);
    void PreReplicatedRemove(const FQuestRuntimeArray& Array);

    FQuestTrackerEntry* FindTracker(const FGameplayTag& Key)
    {
        return Trackers.FindByPredicate(
            [&](const FQuestTrackerEntry& E){ return E.TrackerKey == Key; });
    }
    const FQuestTrackerEntry* FindTracker(const FGameplayTag& Key) const
    {
        return Trackers.FindByPredicate(
            [&](const FQuestTrackerEntry& E){ return E.TrackerKey == Key; });
    }
};

USTRUCT()
struct GAMECORE_API FQuestRuntimeArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FQuestRuntime> Items;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<
            FQuestRuntime, FQuestRuntimeArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FQuestRuntimeArray>
    : public TStructOpsTypeTraitsBase2<FQuestRuntimeArray>
{
    enum { WithNetDeltaSerializer = true };
};
