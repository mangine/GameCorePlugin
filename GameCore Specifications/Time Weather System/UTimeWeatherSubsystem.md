# UTimeWeatherSubsystem

**Sub-page of:** [Time Weather System](../Time%20Weather%20System.md)

`UWorldSubsystem`. Server-authority. Owns time advancement, season state, daily weather schedule construction, base weather blend tracking, and overlay event management.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UTimeWeatherSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // --- Public Query API ---

    // Current time snapshot. Safe to call from any server system.
    const FGameTimeSnapshot& GetTimeSnapshot() const { return TimeSnapshot; }

    // Current blend state for the global context or a specific region context.
    // Pass nullptr for global.
    FWeatherBlendState GetCurrentWeatherState(
        const IWeatherContextProvider* Provider = nullptr) const;

    // Current normalized day time [0,1) on the server.
    float GetNormalizedDayTime() const;

    // Current season tag for a given context (invalid if no seasons defined).
    FGameplayTag GetCurrentSeasonTag(
        const IWeatherContextProvider* Provider = nullptr) const;

    // Region boundary blend helper. Spatial alpha driven by area system.
    static FWeatherBlendState GetBlendedWeatherState(
        const FWeatherBlendState& A,
        const FWeatherBlendState& B,
        float SpatialAlpha);

    // --- Event API ---

    // Trigger an overlay event on a context immediately.
    // Returns a FGuid handle for cancellation. Null provider = global context.
    FGuid TriggerOverlayEvent(
        UWeatherEventDefinition*       Event,
        const IWeatherContextProvider* Provider = nullptr);

    // Cancel an in-flight overlay event by its handle. Begins immediate fade-out
    // instead of abrupt removal.
    void CancelOverlayEvent(FGuid EventId);

    // --- Region Registration ---

    void RegisterContextProvider(IWeatherContextProvider* Provider);
    void UnregisterContextProvider(IWeatherContextProvider* Provider);

    // --- UWorldSubsystem ---
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

private:
    // --- Config ---
    UPROPERTY() TObjectPtr<UTimeWeatherConfig> Config;

    // --- Time State ---
    FGameTimeSnapshot TimeSnapshot;
    int32             LastKnownDay    = -1;
    float             LastDayTime     = 0.f;
    bool              bDawnFired      = false;
    bool              bDuskFired      = false;

    // --- Per-Context Runtime State ---
    // Keyed by FGuid from IWeatherContextProvider::GetContextId().
    // Entry 0 (NullGuid) = global context.
    struct FContextState
    {
        UWeatherContextAsset*      Context          = nullptr;
        IWeatherContextProvider*   Provider         = nullptr; // null = global
        FDailyWeatherSchedule      DailySchedule;
        FWeatherBlendState         CurrentBlend;
        TArray<FActiveWeatherEvent> ActiveEvents;    // sorted by Priority desc
        TArray<FActiveWeatherEvent> QueuedEvents;    // waiting for active slot
        FGameplayTag               LastBuiltSeason;  // for schedule rebuild on season change
    };

    TMap<FGuid, FContextState>   ContextStates;  // FGuid = provider context ID
    TArray<IWeatherContextProvider*> RegisteredProviders;

    // --- Internal Methods ---

    void InitGlobalContext();
    void RebuildDailySchedule(FContextState& State, int32 Day);
    void TickContextState(FContextState& State, float NormDayTime, double NowSeconds);
    void AdvanceBaseBlend(FContextState& State, float NormDayTime);
    void TickOverlayEvents(FContextState& State, double NowSeconds);
    void ActivateQueuedEvent(FContextState& State);
    void BroadcastWeatherChanged(const FContextState& State);
    void BroadcastTimeEvents(float NormDayTime, int32 CurrentDay);
    FRandomStream MakeSeededStream(int32 Day, FGuid ContextId) const;

    // Replicate snapshot to all clients via AGameState.
    void PushSnapshotToClients();
};
```

**Note:** `ShouldCreateSubsystem` returns `false` on clients — the subsystem is server-only. Clients receive `FGameTimeSnapshot` via a replicated property on `AGameState`.

---

## Initialize

```cpp
void UTimeWeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Load config via Asset Manager or project settings.
    Config = GetDefault<UTimeWeatherProjectSettings>()->TimeWeatherConfig.LoadSynchronous();
    if (!Config)
    {
        UE_LOG(LogGameCore, Error, TEXT("UTimeWeatherSubsystem: No UTimeWeatherConfig set. System inactive."));
        return;
    }

    // Build snapshot from config.
    TimeSnapshot.ServerEpochOffsetSeconds = Config->ServerEpochOffsetSeconds;
    TimeSnapshot.DayDurationSeconds       = Config->DayDurationSeconds;
    TimeSnapshot.YearLengthDays           = Config->YearLengthDays;

    // Init global context state.
    InitGlobalContext();

    LastKnownDay = TimeSnapshot.GetCurrentDay(FPlatformTime::Seconds());
    PushSnapshotToClients();
}

void UTimeWeatherSubsystem::InitGlobalContext()
{
    FContextState& Global = ContextStates.FindOrAdd(FGuid());
    Global.Context  = Config->GlobalContext;
    Global.Provider = nullptr;

    int32 Day = TimeSnapshot.GetCurrentDay(FPlatformTime::Seconds());
    RebuildDailySchedule(Global, Day);
}
```

---

## Tick

```cpp
void UTimeWeatherSubsystem::Tick(float DeltaTime)
{
    if (!Config) return;

    const int64 NowUnix    = (int64)FPlatformTime::Seconds();
    const float NormDay    = TimeSnapshot.GetNormalizedDayTime(NowUnix);
    const int32 CurrentDay = TimeSnapshot.GetCurrentDay(NowUnix);
    const double NowSec    = FPlatformTime::Seconds();

    // Day rollover.
    if (CurrentDay != LastKnownDay)
    {
        LastKnownDay = CurrentDay;
        bDawnFired   = false;
        bDuskFired   = false;

        // Rebuild schedules for all contexts.
        for (auto& [Id, State] : ContextStates)
            RebuildDailySchedule(State, CurrentDay);

        BroadcastTimeEvents(NormDay, CurrentDay); // fires DayRolledOver
        PushSnapshotToClients();
    }

    BroadcastTimeEvents(NormDay, CurrentDay); // fires Dawn/Dusk once per day

    // Tick every context.
    for (auto& [Id, State] : ContextStates)
        TickContextState(State, NormDay, NowSec);

    LastDayTime = NormDay;
}
```

---

## RebuildDailySchedule

Called once per day per context. Builds an ordered array of `FDailyWeatherKeyframe` from the active sequence. All randomness is derived from a seeded stream — same seed = same schedule.

```cpp
void UTimeWeatherSubsystem::RebuildDailySchedule(
    FContextState& State, int32 Day)
{
    State.DailySchedule.Keyframes.Reset();
    State.DailySchedule.CurrentKeyframeIndex = 0;

    UWeatherContextAsset* Ctx = State.Context;
    if (!Ctx) return;

    // Resolve active season and sequence.
    FSeasonContext SeasonCtx = ResolveSeasonContext(Ctx, Day);
    UWeatherSequence* Sequence = ResolveSequence(Ctx, SeasonCtx);
    if (!Sequence) return;

    FGuid ContextId = State.Provider
        ? State.Provider->GetContextId()
        : FGuid();
    FRandomStream Stream = MakeSeededStream(Day, ContextId);

    int32 NumKeyframes = Sequence->GetKeyframesPerDay();
    float SlotSize     = 1.f / (float)NumKeyframes;
    FGameplayTag Current = State.CurrentBlend.BaseWeatherB; // last resolved weather

    for (int32 i = 0; i < NumKeyframes; ++i)
    {
        float TransitionSec = 0.f;
        FGameplayTag Next = Sequence->GetNextWeather(Current, SeasonCtx, Stream, TransitionSec);

        FDailyWeatherKeyframe& KF = State.DailySchedule.Keyframes.AddDefaulted_GetRef();
        KF.StartNormTime     = i * SlotSize;
        KF.EndNormTime       = (i + 1) * SlotSize;
        KF.WeatherTag        = Next;
        KF.TransitionDuration = TransitionSec;

        Current = Next;
    }

    // Roll probabilistic timed events.
    if (SeasonCtx.CurrentSeason.IsValid())
        RollTimedEvents(State, SeasonCtx, Stream, Day);
}
```

---

## TickContextState

```cpp
void UTimeWeatherSubsystem::TickContextState(
    FContextState& State, float NormDayTime, double NowSeconds)
{
    FWeatherBlendState PrevBlend = State.CurrentBlend;

    AdvanceBaseBlend(State, NormDayTime);
    TickOverlayEvents(State, NowSeconds);

    // Broadcast only if state changed meaningfully.
    // Use tag comparison — avoid broadcasting on micro float changes.
    if (State.CurrentBlend.BaseWeatherA  != PrevBlend.BaseWeatherA  ||
        State.CurrentBlend.BaseWeatherB  != PrevBlend.BaseWeatherB  ||
        State.CurrentBlend.OverlayWeather != PrevBlend.OverlayWeather)
    {
        BroadcastWeatherChanged(State);
    }
}
```

---

## AdvanceBaseBlend

```cpp
void UTimeWeatherSubsystem::AdvanceBaseBlend(
    FContextState& State, float NormDayTime)
{
    const FDailyWeatherKeyframe* KF =
        State.DailySchedule.GetActiveKeyframe(NormDayTime);
    if (!KF) return;

    FWeatherBlendState& Blend = State.CurrentBlend;

    // New keyframe started — shift B into A, set new target.
    if (Blend.BaseWeatherB != KF->WeatherTag)
    {
        Blend.BaseWeatherA = Blend.BaseWeatherB;
        Blend.BaseWeatherB = KF->WeatherTag;
        Blend.BlendAlpha   = 0.f;
    }

    // Advance alpha based on real time vs transition duration.
    if (KF->TransitionDuration > 0.f)
    {
        float KeyframeDuration = (KF->EndNormTime - KF->StartNormTime)
            * TimeSnapshot.DayDurationSeconds;
        float Elapsed = (NormDayTime - KF->StartNormTime)
            * TimeSnapshot.DayDurationSeconds;
        Blend.BlendAlpha = FMath::Clamp(
            Elapsed / KF->TransitionDuration, 0.f, 1.f);
    }
    else
    {
        Blend.BlendAlpha = 1.f;
    }
}
```

---

## TickOverlayEvents

```cpp
void UTimeWeatherSubsystem::TickOverlayEvents(
    FContextState& State, double NowSeconds)
{
    // Tick active events and expire finished ones.
    for (int32 i = State.ActiveEvents.Num() - 1; i >= 0; --i)
    {
        FActiveWeatherEvent& Ev = State.ActiveEvents[i];
        Ev.TickAlpha(NowSeconds);

        if (Ev.IsExpired(NowSeconds))
        {
            BroadcastEventCompleted(Ev);
            // Unregister from shared active event registry.
            UnregisterFromEventRegistry(Ev.EventId);
            State.ActiveEvents.RemoveAt(i);
            ActivateQueuedEvent(State); // promote next from queue
        }
    }

    // Resolve overlay alpha: highest-priority active event.
    // ActiveEvents is kept sorted by Priority desc at insertion time.
    FWeatherBlendState& Blend = State.CurrentBlend;
    if (State.ActiveEvents.IsEmpty())
    {
        Blend.OverlayWeather = FGameplayTag::EmptyTag;
        Blend.OverlayAlpha   = 0.f;
    }
    else
    {
        const FActiveWeatherEvent& Top = State.ActiveEvents[0];
        Blend.OverlayWeather = Top.Definition->WeatherTag;
        Blend.OverlayAlpha   = Top.CurrentAlpha;
    }
}
```

---

## TriggerOverlayEvent

```cpp
FGuid UTimeWeatherSubsystem::TriggerOverlayEvent(
    UWeatherEventDefinition* Event,
    const IWeatherContextProvider* Provider)
{
    if (!Event) return FGuid();

    FGuid ContextId = Provider ? Provider->GetContextId() : FGuid();
    FContextState* State = ContextStates.Find(ContextId);
    if (!State) return FGuid();

    // Check if an active event of equal or higher priority exists.
    int32 NewPriority = Event->Priority;
    bool bShouldQueue = false;
    for (const FActiveWeatherEvent& Existing : State->ActiveEvents)
    {
        if (Existing.Priority >= NewPriority)
        {
            bShouldQueue = true;
            break;
        }
    }

    double Now = FPlatformTime::Seconds();
    FActiveWeatherEvent NewEvent;
    NewEvent.EventId    = FGuid::NewGuid();
    NewEvent.Definition = Event;
    NewEvent.Priority   = NewPriority;
    NewEvent.StartTime  = Now;
    NewEvent.FadeInEnd  = Now + Event->FadeInSeconds;
    NewEvent.SustainEnd = Now + Event->FadeInSeconds + Event->SustainSeconds;
    NewEvent.FadeOutEnd = Now + Event->TotalDurationSeconds();

    if (bShouldQueue)
    {
        // Insert into queue sorted by priority desc, then insertion order (append same-priority).
        int32 InsertIdx = State->QueuedEvents.Num();
        for (int32 i = 0; i < State->QueuedEvents.Num(); ++i)
        {
            if (State->QueuedEvents[i].Priority < NewPriority)
            { InsertIdx = i; break; }
        }
        State->QueuedEvents.Insert(NewEvent, InsertIdx);
    }
    else
    {
        // Becomes the new top — insert at front of ActiveEvents.
        State->ActiveEvents.Insert(NewEvent, 0);
        RegisterInEventRegistry(NewEvent);
        BroadcastEventActivated(NewEvent, State);
    }

    return NewEvent.EventId;
}
```

---

## CancelOverlayEvent

```cpp
void UTimeWeatherSubsystem::CancelOverlayEvent(FGuid EventId)
{
    for (auto& [ContextId, State] : ContextStates)
    {
        // Search active events.
        for (int32 i = 0; i < State.ActiveEvents.Num(); ++i)
        {
            if (State.ActiveEvents[i].EventId == EventId)
            {
                FActiveWeatherEvent& Ev = State.ActiveEvents[i];
                // Begin immediate fade-out from current alpha.
                double Now = FPlatformTime::Seconds();
                float RemainingFade = Ev.Definition->FadeOutSeconds
                    * Ev.CurrentAlpha; // proportional to how much is still visible
                Ev.SustainEnd = Now;
                Ev.FadeOutEnd = Now + RemainingFade;
                return;
            }
        }
        // Search queued events — simply remove.
        State.QueuedEvents.RemoveAll(
            [&](const FActiveWeatherEvent& E){ return E.EventId == EventId; });
    }
}
```

---

## MakeSeededStream

```cpp
FRandomStream UTimeWeatherSubsystem::MakeSeededStream(
    int32 Day, FGuid ContextId) const
{
    // Combine base seed + day + context ID into a stable integer seed.
    uint32 Seed = HashCombine(
        GetTypeHash(Config->WeatherSeedBase),
        HashCombine(GetTypeHash(Day), GetTypeHash(ContextId)));
    return FRandomStream((int32)Seed);
}
```

---

## Day-Night Curve Query

Not a subsystem method — any system queries the curve directly:

```cpp
// Utility in TimeWeatherSubsystem.h:
float GetDaylightIntensity(
    const UWeatherContextAsset* Context,
    const FSeasonContext& SeasonCtx,
    float NormDayTime);
```

Resolves: season `DayNightCurve` → context `DefaultDayNightCurve` → returns 0.0 (night fallback). External systems call this; the subsystem does not push daylight values proactively.
