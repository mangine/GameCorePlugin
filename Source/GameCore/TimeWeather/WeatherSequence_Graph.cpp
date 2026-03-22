#include "WeatherSequence_Graph.h"
#include "GameCore.h"

FGameplayTag UWeatherSequence_Graph::GetNextWeather(
    FGameplayTag Current, const FSeasonContext& Context,
    FRandomStream& Stream, float& OutTransitionSeconds)
{
    FGameplayTag Source = Current.IsValid() ? Current : InitialWeather;
    const FWeatherGraphNode* Node = FindNode(Source);

    if (!Node)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("WeatherSequence_Graph: no node for [%s], using InitialWeather"),
            *Source.ToString());
        Node = FindNode(InitialWeather);
    }
    if (!Node || Node->Edges.IsEmpty())
    {
        OutTransitionSeconds = 0.f;
        return Source; // stay in place
    }

    // Filter edges by season.
    TArray<const FWeatherGraphEdge*> ActiveEdges;
    for (const FWeatherGraphEdge& Edge : Node->Edges)
    {
        if (!Edge.SeasonFilter.IsValid() ||
            Edge.SeasonFilter == Context.CurrentSeason)
            ActiveEdges.Add(&Edge);
    }
    if (ActiveEdges.IsEmpty()) ActiveEdges.Add(&Node->Edges[0]); // fallback

    // Weighted pick.
    float TotalWeight = 0.f;
    for (const FWeatherGraphEdge* E : ActiveEdges) TotalWeight += E->Weight;
    float Roll = Stream.FRandRange(0.f, TotalWeight);
    float Acc  = 0.f;
    const FWeatherGraphEdge* Chosen = ActiveEdges.Last();
    for (const FWeatherGraphEdge* E : ActiveEdges)
    {
        Acc += E->Weight;
        if (Roll <= Acc) { Chosen = E; break; }
    }

    OutTransitionSeconds = Stream.FRandRange(
        Chosen->TransitionMinSeconds, Chosen->TransitionMaxSeconds);
    return Chosen->ToWeather;
}

EDataValidationResult UWeatherSequence_Graph::IsDataValid(
    FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);
    if (!InitialWeather.IsValid())
    {
        Context.AddError(FText::FromString(
            TEXT("UWeatherSequence_Graph: InitialWeather must be set.")));
        Result = EDataValidationResult::Invalid;
    }
    if (Nodes.IsEmpty())
    {
        Context.AddError(FText::FromString(
            TEXT("UWeatherSequence_Graph: Nodes array is empty.")));
        Result = EDataValidationResult::Invalid;
    }
    for (const FWeatherGraphNode& Node : Nodes)
    {
        if (Node.Edges.IsEmpty())
        {
            Context.AddWarning(FText::FromString(FString::Printf(
                TEXT("UWeatherSequence_Graph: Node [%s] has no outgoing edges — weather will be stuck."),
                *Node.WeatherTag.ToString())));
        }
    }
    return Result;
}

const FWeatherGraphNode* UWeatherSequence_Graph::FindNode(FGameplayTag Tag) const
{
    for (const FWeatherGraphNode& Node : Nodes)
        if (Node.WeatherTag == Tag)
            return &Node;
    return nullptr;
}
