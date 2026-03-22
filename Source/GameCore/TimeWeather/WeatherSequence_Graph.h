#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TimeWeather/WeatherSequence.h"
#include "WeatherSequence_Graph.generated.h"

// =============================================================================
// FWeatherGraphEdge
// =============================================================================

USTRUCT(BlueprintType)
struct FWeatherGraphEdge
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly) FGameplayTag ToWeather;

    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f))
    float Weight = 1.f;

    /** Edge is only active during this season. Invalid tag = always active. */
    UPROPERTY(EditDefaultsOnly)
    FGameplayTag SeasonFilter;

    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f))
    float TransitionMinSeconds = 120.f;

    UPROPERTY(EditDefaultsOnly, meta=(ClampMin=0.f))
    float TransitionMaxSeconds = 300.f;
};

// =============================================================================
// FWeatherGraphNode
// =============================================================================

USTRUCT(BlueprintType)
struct FWeatherGraphNode
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly) FGameplayTag WeatherTag;
    UPROPERTY(EditDefaultsOnly) TArray<FWeatherGraphEdge> Edges;
};

// =============================================================================
// UWeatherSequence_Graph
// =============================================================================

/**
 * Explicit state-machine adjacency list.
 * Best for biomes where transitions must be tightly controlled or season-gated.
 */
UCLASS(BlueprintType)
class GAMECORE_API UWeatherSequence_Graph : public UWeatherSequence
{
    GENERATED_BODY()
public:
    /** Initial weather when the graph has no prior state (first keyframe of first day). */
    UPROPERTY(EditDefaultsOnly, Category="Graph")
    FGameplayTag InitialWeather;

    UPROPERTY(EditDefaultsOnly, Category="Graph")
    TArray<FWeatherGraphNode> Nodes;

    UPROPERTY(EditDefaultsOnly, Category="Graph")
    int32 KeyframesPerDay = 3;

    virtual FGameplayTag GetNextWeather(
        FGameplayTag          Current,
        const FSeasonContext& Context,
        FRandomStream&        Stream,
        float&                OutTransitionSeconds) override;

    virtual int32 GetKeyframesPerDay() const override { return KeyframesPerDay; }

    /** Editor validation: warns if any node has no outgoing edges for a given season. */
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;

private:
    const FWeatherGraphNode* FindNode(FGameplayTag Tag) const;
};
