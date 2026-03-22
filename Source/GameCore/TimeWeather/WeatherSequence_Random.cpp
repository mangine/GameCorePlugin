#include "WeatherSequence_Random.h"

FGameplayTag UWeatherSequence_Random::GetNextWeather(
    FGameplayTag Current, const FSeasonContext& Context,
    FRandomStream& Stream, float& OutTransitionSeconds)
{
    TArray<const FWeightedWeather*> Eligible = GetEligibleEntries(Current);

    // If predecessor filtering leaves nothing, fall back to full unfiltered set.
    if (Eligible.IsEmpty())
    {
        for (const FWeightedWeather& E : Entries)
            Eligible.Add(&E);
    }

    const FWeightedWeather* Picked = PickWeighted(Eligible, Stream);
    if (!Picked)
    {
        OutTransitionSeconds = 0.f;
        return FGameplayTag::EmptyTag;
    }

    OutTransitionSeconds = Stream.FRandRange(
        Picked->TransitionMinSeconds, Picked->TransitionMaxSeconds);
    return Picked->WeatherTag;
}

TArray<const FWeightedWeather*> UWeatherSequence_Random::GetEligibleEntries(
    FGameplayTag Current) const
{
    TArray<const FWeightedWeather*> Result;
    for (const FWeightedWeather& E : Entries)
    {
        if (E.ValidPredecessors.IsEmpty() ||
            E.ValidPredecessors.Contains(Current))
        {
            Result.Add(&E);
        }
    }
    return Result;
}

const FWeightedWeather* UWeatherSequence_Random::PickWeighted(
    const TArray<const FWeightedWeather*>& Eligible,
    FRandomStream& Stream) const
{
    if (Eligible.IsEmpty()) return nullptr;

    float TotalWeight = 0.f;
    for (const FWeightedWeather* E : Eligible) TotalWeight += E->Weight;

    float Roll = Stream.FRandRange(0.f, TotalWeight);
    float Acc  = 0.f;
    for (const FWeightedWeather* E : Eligible)
    {
        Acc += E->Weight;
        if (Roll <= Acc) return E;
    }
    return Eligible.Last();
}
