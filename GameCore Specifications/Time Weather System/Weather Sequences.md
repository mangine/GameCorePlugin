# Weather Sequences

**Sub-page of:** [Time Weather System](../Time%20Weather%20System.md)

Covers `UWeatherSequence` (abstract base), `UWeatherSequence_Random`, and `UWeatherSequence_Graph`.

---

## UWeatherSequence (Abstract)

The single interface between the subsystem and weather selection logic. The subsystem calls `GetNextWeather` once per keyframe boundary during daily schedule construction. It never calls it per-tick.

```cpp
UCLASS(Abstract, EditInlineNew, BlueprintType, Blueprintable)
class GAMECORE_API UWeatherSequence : public UObject
{
    GENERATED_BODY()
public:
    /**
     * Returns the next weather tag to transition to and the real-time duration
     * of the transition (blend alpha 0→1).
     *
     * Called once per keyframe slot during daily schedule construction at dawn.
     * Must be deterministic given the same Seed — called with a seeded FRandomStream.
     * Must NOT access world state, components, or subsystems.
     *
     * @param CurrentWeather  Tag of the weather ending (invalid on first call of the day).
     * @param Context         Season and time context at the call site.
     * @param Stream          Seeded random stream for this day+context. Use this exclusively.
     * @param OutTransitionSeconds  Set to real-time seconds for the blend transition.
     * @return  Tag of the next weather state.
     */
    virtual FGameplayTag GetNextWeather(
        FGameplayTag          CurrentWeather,
        const FSeasonContext& Context,
        FRandomStream&        Stream,
        float&                OutTransitionSeconds) PURE_VIRTUAL(GetNextWeather, return FGameplayTag(););

    /**
     * How many weather keyframes to generate per day for this context.
     * Default: 3 (morning / afternoon / evening).
     * Override to 1 for a stable all-day weather context.
     */
    virtual int32 GetKeyframesPerDay() const { return 3; }
};
```

**Contract:**
- Output must be deterministic per `(Stream, CurrentWeather, Context)` triplet.
- Never store mutable state — the same sequence object may be called for multiple contexts.
- `OutTransitionSeconds` should be ≤ `(1.f / GetKeyframesPerDay()) * DayDurationSeconds` to avoid a transition spanning into the next keyframe.

---

## UWeatherSequence_Random

Weighted random selection with optional neighbour filtering. Best for open-world regions where broad weather variety is desired but implausible day-to-day jumps should be prevented.

```cpp
USTRUCT(BlueprintType)
struct FWeightedWeather
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly) FGameplayTag WeatherTag;

    // Relative probability weight. Higher = more frequent.
    UPROPERTY(EditDefaultsOnly, Meta=(ClampMin=0.f))
    float Weight = 1.f;

    // If non-empty, this weather can only follow one of these tags.
    // Empty = can follow any weather.
    UPROPERTY(EditDefaultsOnly)
    TArray<FGameplayTag> ValidPredecessors;

    // Transition duration range (real seconds). Actual value randomised per call.
    UPROPERTY(EditDefaultsOnly, Meta=(ClampMin=0.f))
    float TransitionMinSeconds = 120.f;

    UPROPERTY(EditDefaultsOnly, Meta=(ClampMin=0.f))
    float TransitionMaxSeconds = 300.f;
};

UCLASS(BlueprintType)
class GAMECORE_API UWeatherSequence_Random : public UWeatherSequence
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Sequence")
    TArray<FWeightedWeather> Entries;

    UPROPERTY(EditDefaultsOnly, Category="Sequence")
    int32 KeyframesPerDay = 3;

    virtual FGameplayTag GetNextWeather(FGameplayTag Current,
        const FSeasonContext& Context, FRandomStream& Stream,
        float& OutTransitionSeconds) override;

    virtual int32 GetKeyframesPerDay() const override { return KeyframesPerDay; }

private:
    // Builds eligible subset: entries whose ValidPredecessors allow Current.
    TArray<const FWeightedWeather*> GetEligibleEntries(FGameplayTag Current) const;

    // Weighted random pick from eligible set using Stream.
    const FWeightedWeather* PickWeighted(
        const TArray<const FWeightedWeather*>& Eligible,
        FRandomStream& Stream) const;
};
```

**GetNextWeather implementation:**
```cpp
FGameplayTag UWeatherSequence_Random::GetNextWeather(
    FGameplayTag Current, const FSeasonContext& Context,
    FRandomStream& Stream, float& OutTransitionSeconds)
{
    TArray<const FWeightedWeather*> Eligible = GetEligibleEntries(Current);

    // If filtering leaves nothing, fall back to unfiltered set.
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
```

**ValidPredecessors vs ValidFollowers note:** Using predecessors ("I can follow X") rather than followers ("X can be followed by") is intentional — it keeps constraint authoring on the *incoming* weather entry, making it easy to add a new weather type without editing existing ones.

---

## UWeatherSequence_Graph

Explicit state-machine adjacency list. Best for biomes where transitions must be tightly controlled or season-gated. Heavier to author; zero ambiguity about valid transitions.

```cpp
USTRUCT(BlueprintType)
struct FWeatherGraphEdge
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly) FGameplayTag ToWeather;
    UPROPERTY(EditDefaultsOnly, Meta=(ClampMin=0.f)) float Weight = 1.f;

    // Edge is only active during this season. Invalid tag = always active.
    UPROPERTY(EditDefaultsOnly) FGameplayTag SeasonFilter;

    UPROPERTY(EditDefaultsOnly, Meta=(ClampMin=0.f)) float TransitionMinSeconds = 120.f;
    UPROPERTY(EditDefaultsOnly, Meta=(ClampMin=0.f)) float TransitionMaxSeconds = 300.f;
};

USTRUCT(BlueprintType)
struct FWeatherGraphNode
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly) FGameplayTag WeatherTag;  // this node's identity
    UPROPERTY(EditDefaultsOnly) TArray<FWeatherGraphEdge> Edges;
};

UCLASS(BlueprintType)
class GAMECORE_API UWeatherSequence_Graph : public UWeatherSequence
{
    GENERATED_BODY()
public:
    // Initial weather when the graph has no prior state (first keyframe of the server's first day).
    UPROPERTY(EditDefaultsOnly, Category="Graph")
    FGameplayTag InitialWeather;

    UPROPERTY(EditDefaultsOnly, Category="Graph")
    TArray<FWeatherGraphNode> Nodes;

    UPROPERTY(EditDefaultsOnly, Category="Graph")
    int32 KeyframesPerDay = 3;

    virtual FGameplayTag GetNextWeather(FGameplayTag Current,
        const FSeasonContext& Context, FRandomStream& Stream,
        float& OutTransitionSeconds) override;

    virtual int32 GetKeyframesPerDay() const override { return KeyframesPerDay; }

    // Editor validation: warns if any node has no outgoing edges for a given season.
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

private:
    const FWeatherGraphNode* FindNode(FGameplayTag Tag) const;
};
```

**GetNextWeather implementation:**
```cpp
FGameplayTag UWeatherSequence_Graph::GetNextWeather(
    FGameplayTag Current, const FSeasonContext& Context,
    FRandomStream& Stream, float& OutTransitionSeconds)
{
    // On first call (no prior state), use InitialWeather.
    FGameplayTag Source = Current.IsValid() ? Current : InitialWeather;
    const FWeatherGraphNode* Node = FindNode(Source);

    if (!Node)
    {
        UE_LOG(LogGameCore, Warning, TEXT("WeatherSequence_Graph: no node for [%s], using InitialWeather"),
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
```

---

## Choosing a Sequence Type

| Scenario | Recommended Type |
|---|---|
| Open ocean, open world — broad variety, soft constraints | `UWeatherSequence_Random` with `ValidPredecessors` |
| Permafrost / desert — one or two weathers, minimal transitions | `UWeatherSequence_Random` with 1–2 entries |
| Boss arena, dungeon entrance — strict atmosphere control | `UWeatherSequence_Graph` |
| City with seasonal flavour — moderate control | `UWeatherSequence_Graph` with season-filtered edges |
| Dead simple "always foggy" | `UWeatherSequence_Random` with 1 entry, `KeyframesPerDay = 1` |
