# UTimeWeatherSubsystem

**Sub-page of:** [Time Weather System](../Time%20Weather%20System.md)

`UWorldSubsystem`. Server-authority only. Owns time advancement, season state, daily weather schedule construction, base weather blend tracking, and overlay event management for all registered contexts.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UTimeWeatherSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    // Public Query API
    // -----------------------------------------------------------------------

    // Current time snapshot (stateless config). Safe to call from any server system.
    const FGameTimeSnapshot& GetTimeSnapshot() const { return TimeSnapshot; }

    // Current normalized day time [0,1) on the server.
    float GetNormalizedDayTime() const;

    // Current blend state for the global context, or for a specific region context.
    // Pass nullptr (or omit) for the global context.
    FWeatherBlendState GetCurrentWeatherState(
        const IWeatherContextProvider* Provider = nullptr) const;

    // Current season tag for a context. Returns an invalid tag if no seasons are defined.
    FGameplayTag GetCurrentSeasonTag(
        const IWeatherContextProvider* Provider = nullptr) const;

    // Daylight intensity [0,1] for a context at the current moment.
    // Resolves: season curve → context default curve → 0.0 fallback.
    float GetDaylightIntensity(
        const IWeatherContextProvider* Provider = nullptr) const;

    // Region-boundary blend helper. Spatial alpha is supplied by the area system.
    // 0.0 = pure StateA, 1.0 = pure StateB. Tags are winner-takes-all at 0.5.
    static FWeatherBlendState GetBlendedWeatherState(
        const FWeatherBlendState& StateA,
        const FWeatherBlendState& StateB,
        float SpatialAlpha);

    // -----------------------------------------------------------------------
    // Event API
    // -----------------------------------------------------------------------

    // Immediately trigger an overlay event on a context.
    // If a higher-or-equal-priority event is already active, this event is queued.
    // Returns a FGuid handle. Pass nullptr for Provider to target the global context.
    // Returns FGuid() (invalid) if Event is null or the context is not registered.
    FGuid TriggerOverlayEvent(
        UWeatherEventDefinition*       Event,
        const IWeatherContextProvider* Provider = nullptr);

    // Cancel an in-flight event. Triggers a proportional fade-out rather than
    // abrupt removal. Safe to call with an invalid or already-expired GUID.
    void CancelOverlayEvent(FGuid EventId);

    // -----------------------------------------------------------------------
    // Region Registration
    // -----------------------------------------------------------------------

    // Call in BeginPlay (server only) to register a region context.
    // Immediately allocates a FContextState and builds the first daily schedule.
    void RegisterContextProvider(IWeatherContextProvider* Provider);

    // Call in EndPlay. Removes the context state and unregisters all its active events.
    void UnregisterContextProvider(IWeatherContextProvider* Provider);

    // -----------------------------------------------------------------------
    // UWorldSubsystem
    // -----------------------------------------------------------------------
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override { return bInitialised; }

private:
    // -----------------------------------------------------------------------
    // Config
    // -----------------------------------------------------------------------
    UPROPERTY() TObjectPtr<UTimeWeatherConfig> Config;
    bool bInitialised = false;

    // -----------------------------------------------------------------------
    // Time State
    // -----------------------------------------------------------------------
    FGameTimeSnapshot TimeSnapshot;
    int32             LastKnownDay = -1;
    bool              bDawnFired   = false;  // reset each day rollover (global threshold)
    bool              bDuskFired   = false;  // reset each day rollover (global threshold)

    // -----------------------------------------------------------------------
    // Scheduled event trigger (produced by RollTimedEvents each dawn)
    // -----------------------------------------------------------------------
    struct FScheduledEventTrigger
    {
        UWeatherEventDefinition* Event           = nullptr;
        float                    TriggerNormTime = 0.f;  // fires when NormDayTime crosses this
        bool                     bFired          = false;
    };

    // -----------------------------------------------------------------------
    // Per-Context Runtime State
    // -----------------------------------------------------------------------
    struct FContextState
    {
        UWeatherContextAsset*    Context  = nullptr;
        IWeatherContextProvider* Provider = nullptr;  // null = global

        // Cached season ranges — rebuilt when context is registered.
        TArray<FSeasonRange> SeasonRanges;

        // Last season tag used to detect season changes across ticks.
        FGameplayTag LastSeasonTag;

        // Daily weather schedule — rebuilt each day rollover.
        FDailyWeatherSchedule DailySchedule;

        // Current blend output — updated each tick.
        FWeatherBlendState CurrentBlend;

        // Active overlay events, sorted by Priority descending.
        // Only the first entry drives OverlayWeather/OverlayAlpha in CurrentBlend.
        TArray<FActiveWeatherEvent> ActiveEvents;

        // Events waiting for the active slot to free up, sorted by Priority descending.
        // Equal-priority events are ordered by insertion time (first-in wins).
        TArray<FActiveWeatherEvent> QueuedEvents;

        // Probabilistic events scheduled for today. Checked each tick.
        TArray<FScheduledEventTrigger> ScheduledEventTriggers;

        // Dawn/dusk fired flags — per-context because contexts may have independent thresholds.
        bool bDawnFired = false;
        bool bDuskFired = false;
    };

    // Keyed by IWeatherContextProvider::GetContextId().
    // FGuid() (null GUID) = global context.
    TMap<FGuid, FContextState> ContextStates;

    // Raw interface pointers — safe only because we own the lifetime via Register/Unregister.
    TArray<IWeatherContextProvider*> RegisteredProviders;

    // -----------------------------------------------------------------------
    // Internal Methods
    // -----------------------------------------------------------------------
    void InitGlobalContext();
    void RebuildContextCaches(FContextState& State);
    void RebuildDailySchedule(FContextState& State, int32 Day);
    void RollTimedEvents(FContextState& State, const FSeasonContext& SeasonCtx,
                         FRandomStream& Stream, int32 Day);
    void TickContextState(FContextState& State, float NormDayTime, double NowSeconds);
    void AdvanceBaseBlend(FContextState& State, float NormDayTime);
    void TickOverlayEvents(FContextState& State, double NowSeconds);
    void TickScheduledTriggers(FContextState& State, float NormDayTime);
    void ActivateQueuedEvent(FContextState& State);
    void RegisterInEventRegistry(FActiveWeatherEvent& Ev);
    void UnregisterFromEventRegistry(FActiveWeatherEvent& Ev);
    void BroadcastWeatherChanged(const FContextState& State);
    void BroadcastSeasonChanged(const FContextState& State,
                                FGameplayTag OldSeason, FGameplayTag NewSeason);
    void BroadcastEventActivated(const FActiveWeatherEvent& Ev, const FContextState& State);
    void BroadcastEventCompleted(const FActiveWeatherEvent& Ev, const FContextState& State);
    void BroadcastTimeEvents(float NormDayTime, int32 CurrentDay);
    void PushSnapshotToClients();

    FSeasonContext    ResolveSeasonContext(FContextState& State, int32 Day) const;
    USeasonDefinition* ResolveActiveSeason(const FContextState& State,
                                           const FSeasonContext& Ctx) const;
    UWeatherSequence* ResolveSequence(const FContextState& State,
                                      const FSeasonContext& Ctx) const;
    UCurveFloat*      ResolveDayNightCurve(const FContextState& State,
                                          const FSeasonContext& Ctx) const;
    FRandomStream     MakeSeededStream(int32 Day, FGuid ContextId) const;

    FGuid GetContextId(const IWeatherContextProvider* Provider) const
    {
        return Provider ? Provider->GetContextId() : FGuid();
    }
};
```

---

## ShouldCreateSubsystem

```cpp
bool UTimeWeatherSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // Server-only. Dedicated server, listen server, and standalone all qualify.
    // Pure clients skip creation entirely.
    if (!Super::ShouldCreateSubsystem(Outer)) return false;
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->GetNetMode() != NM_Client;
}
```

---

## Initialize

```cpp
void UTimeWeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    const UTimeWeatherProjectSettings* Settings =
        GetDefault<UTimeWeatherProjectSettings>();
    if (!Settings || Settings->TimeWeatherConfig.IsNull())
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UTimeWeatherSubsystem: TimeWeatherConfig not set in project settings. System inactive."));
        return;
    }

    Config = Settings->TimeWeatherConfig.LoadSynchronous();
    if (!Config || !Config->GlobalContext)
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UTimeWeatherSubsystem: Config or GlobalContext is null. System inactive."));
        return;
    }

    // Populate the snapshot from config. This is what gets replicated to clients.
    TimeSnapshot.ServerEpochOffsetSeconds = Config->ServerEpochOffsetSeconds;
    TimeSnapshot.DayDurationSeconds       = Config->DayDurationSeconds;
    TimeSnapshot.YearLengthDays           = Config->YearLengthDays;

    InitGlobalContext();

    LastKnownDay = TimeSnapshot.GetCurrentDay(FPlatformTime::Seconds());
    bInitialised = true;

    PushSnapshotToClients();
}
```

---

## Deinitialize

```cpp
void UTimeWeatherSubsystem::Deinitialize()
{
    // Unregister all active events from the shared registry before teardown.
    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
    {
        for (auto& [Id, State] : ContextStates)
        {
            for (FActiveWeatherEvent& Ev : State.ActiveEvents)
                Bus->UnregisterActiveEvent(Ev.RegistryHandle);
        }
    }

    ContextStates.Reset();
    RegisteredProviders.Reset();
    bInitialised = false;

    Super::Deinitialize();
}
```

---

## InitGlobalContext

```cpp
void UTimeWeatherSubsystem::InitGlobalContext()
{
    FContextState& Global = ContextStates.FindOrAdd(FGuid());
    Global.Context  = Config->GlobalContext;
    Global.Provider = nullptr;
    RebuildContextCaches(Global);
    RebuildDailySchedule(Global, TimeSnapshot.GetCurrentDay(FPlatformTime::Seconds()));
}
```

---

## RegisterContextProvider / UnregisterContextProvider

```cpp
void UTimeWeatherSubsystem::RegisterContextProvider(IWeatherContextProvider* Provider)
{
    if (!Provider) return;

    const FGuid Id = Provider->GetContextId();
    if (!Id.IsValid())
    {
        UE_LOG(LogGameCore, Error,
            TEXT("UTimeWeatherSubsystem: Provider returned invalid GUID. Not registered."));
        return;
    }
    if (ContextStates.Contains(Id))
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("UTimeWeatherSubsystem: Context GUID already registered. Duplicate registration ignored."));
        return;
    }

    RegisteredProviders.AddUnique(Provider);
    FContextState& State = ContextStates.Add(Id);
    State.Context  = Provider->GetWeatherContext();
    State.Provider = Provider;
    RebuildContextCaches(State);
    RebuildDailySchedule(State, TimeSnapshot.GetCurrentDay(FPlatformTime::Seconds()));
}

void UTimeWeatherSubsystem::UnregisterContextProvider(IWeatherContextProvider* Provider)
{
    if (!Provider) return;

    const FGuid Id = Provider->GetContextId();
    if (FContextState* State = ContextStates.Find(Id))
    {
        // Clean up active events from the shared registry.
        if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
        {
            for (FActiveWeatherEvent& Ev : State->ActiveEvents)
                Bus->UnregisterActiveEvent(Ev.RegistryHandle);
        }
        ContextStates.Remove(Id);
    }
    RegisteredProviders.Remove(Provider);
}
```

---

## RebuildContextCaches

Called once at registration time to precompute season ranges. Not called per-tick.

```cpp
void UTimeWeatherSubsystem::RebuildContextCaches(FContextState& State)
{
    State.SeasonRanges.Reset();
    if (State.Context)
        State.Context->ComputeSeasonRanges(TimeSnapshot.YearLengthDays, State.SeasonRanges);
}
```

---

## ResolveSeasonContext

Builds the `FSeasonContext` for a given day. Called during `RebuildDailySchedule`.

```cpp
FSeasonContext UTimeWeatherSubsystem::ResolveSeasonContext(
    FContextState& State, int32 Day) const
{
    FSeasonContext Ctx;
    Ctx.CurrentDay       = Day;
    Ctx.DayOfYear        = Day % FMath::Max(1, TimeSnapshot.YearLengthDays);
    Ctx.NormalizedDayTime = 0.f; // schedule is built at rollover; dawn-ish
    Ctx.LastWeather      = State.CurrentBlend.BaseWeatherB;

    if (State.SeasonRanges.IsEmpty())
        return Ctx; // no seasons — Ctx.CurrentSeason remains invalid

    // Find the active season by day-of-year.
    for (int32 i = 0; i < State.SeasonRanges.Num(); ++i)
    {
        const FSeasonRange& R = State.SeasonRanges[i];
        if (R.ContainsDayOfYear(Ctx.DayOfYear))
        {
            Ctx.CurrentSeason = R.Season ? R.Season->SeasonTag : FGameplayTag();

            // Previous season wraps around.
            const int32 PrevIdx = (i == 0)
                ? State.SeasonRanges.Num() - 1
                : i - 1;
            const FSeasonRange& Prev = State.SeasonRanges[PrevIdx];
            Ctx.PreviousSeason = Prev.Season ? Prev.Season->SeasonTag : FGameplayTag();

            // Blend alpha from USeasonDefinition::ComputeSeasonBlendAlpha.
            if (R.Season)
            {
                float DayWithin = R.GetDayWithinSeason(Ctx.DayOfYear);
                Ctx.SeasonBlendAlpha = R.Season->ComputeSeasonBlendAlpha(
                    DayWithin, R.GetDurationDays());
            }
            else
            {
                Ctx.SeasonBlendAlpha = 1.f;
            }
            break;
        }
    }
    return Ctx;
}
```

---

## ResolveSequence / ResolveActiveSeason / ResolveDayNightCurve

```cpp
USeasonDefinition* UTimeWeatherSubsystem::ResolveActiveSeason(
    const FContextState& State, const FSeasonContext& Ctx) const
{
    for (const FSeasonRange& R : State.SeasonRanges)
        if (R.Season && R.Season->SeasonTag == Ctx.CurrentSeason)
            return R.Season;
    return nullptr;
}

UWeatherSequence* UTimeWeatherSubsystem::ResolveSequence(
    const FContextState& State, const FSeasonContext& Ctx) const
{
    if (USeasonDefinition* Season = ResolveActiveSeason(State, Ctx))
        if (Season->WeatherSequence)
            return Season->WeatherSequence;
    return State.Context ? State.Context->DefaultWeatherSequence : nullptr;
}

UCurveFloat* UTimeWeatherSubsystem::ResolveDayNightCurve(
    const FContextState& State, const FSeasonContext& Ctx) const
{
    if (USeasonDefinition* Season = ResolveActiveSeason(State, Ctx))
        if (Season->DayNightCurve)
            return Season->DayNightCurve;
    return State.Context ? State.Context->DefaultDayNightCurve : nullptr;
}
```

---

## RebuildDailySchedule

Called once per day per context at day rollover. Deterministic — same seed produces the same schedule.

```cpp
void UTimeWeatherSubsystem::RebuildDailySchedule(
    FContextState& State, int32 Day)
{
    State.DailySchedule.Reset();
    State.ScheduledEventTriggers.Reset();

    if (!State.Context) return;

    FSeasonContext SeasonCtx = ResolveSeasonContext(State, Day);

    // Detect season change and broadcast.
    if (SeasonCtx.CurrentSeason != State.LastSeasonTag)
    {
        BroadcastSeasonChanged(State, State.LastSeasonTag, SeasonCtx.CurrentSeason);
        State.LastSeasonTag = SeasonCtx.CurrentSeason;

        // Propagate season into current blend immediately.
        State.CurrentBlend.CurrentSeason    = SeasonCtx.CurrentSeason;
        State.CurrentBlend.PreviousSeason   = SeasonCtx.PreviousSeason;
        State.CurrentBlend.SeasonBlendAlpha = SeasonCtx.SeasonBlendAlpha;
    }

    UWeatherSequence* Sequence = ResolveSequence(State, SeasonCtx);
    if (!Sequence)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("UTimeWeatherSubsystem: No weather sequence resolved for context. Schedule empty."));
        return;
    }

    const FGuid ContextId = GetContextId(State.Provider);
    FRandomStream Stream  = MakeSeededStream(Day, ContextId);

    const int32  NumKeyframes = Sequence->GetKeyframesPerDay();
    const float  SlotSize     = 1.f / (float)FMath::Max(1, NumKeyframes);
    FGameplayTag Current      = State.CurrentBlend.BaseWeatherB; // cross-day continuity

    for (int32 i = 0; i < NumKeyframes; ++i)
    {
        float TransitionSec = 0.f;
        FGameplayTag Next = Sequence->GetNextWeather(Current, SeasonCtx, Stream, TransitionSec);

        FDailyWeatherKeyframe& KF = State.DailySchedule.Keyframes.AddDefaulted_GetRef();
        KF.StartNormTime     = i * SlotSize;
        KF.EndNormTime       = (i + 1) * SlotSize;
        KF.WeatherTag        = Next;
        KF.TransitionDuration = FMath::Clamp(TransitionSec, 0.f,
            SlotSize * TimeSnapshot.DayDurationSeconds); // never exceed slot duration
        Current = Next;
    }

    // Roll probabilistic timed events using the same stream (order-dependent — always after keyframes).
    RollTimedEvents(State, SeasonCtx, Stream, Day);

    State.bDawnFired = false;
    State.bDuskFired = false;
}
```

---

## RollTimedEvents

```cpp
void UTimeWeatherSubsystem::RollTimedEvents(
    FContextState& State,
    const FSeasonContext& SeasonCtx,
    FRandomStream& Stream,
    int32 Day)
{
    USeasonDefinition* Season = ResolveActiveSeason(State, SeasonCtx);
    if (!Season) return;

    for (const FSeasonWeatherEvent& SE : Season->TimedEvents)
    {
        if (!SE.Event) continue;
        if (SE.WindowEnd <= SE.WindowStart) continue;

        // Probability roll.
        if (Stream.GetFraction() > SE.DailyProbability) continue;

        // Random start time within the window.
        float TriggerNorm = Stream.FRandRange(SE.WindowStart, SE.WindowEnd);

        FScheduledEventTrigger& Trigger =
            State.ScheduledEventTriggers.AddDefaulted_GetRef();
        Trigger.Event           = SE.Event;
        Trigger.TriggerNormTime = TriggerNorm;
        Trigger.bFired          = false;
    }

    // Sort by trigger time so TickScheduledTriggers can break early.
    State.ScheduledEventTriggers.Sort(
        [](const FScheduledEventTrigger& A, const FScheduledEventTrigger& B)
        { return A.TriggerNormTime < B.TriggerNormTime; });
}
```

---

## Tick

```cpp
void UTimeWeatherSubsystem::Tick(float DeltaTime)
{
    if (!bInitialised) return;

    const double NowSec    = FPlatformTime::Seconds();
    const float  NormDay   = TimeSnapshot.GetNormalizedDayTime(NowSec);
    const int32  CurrentDay = TimeSnapshot.GetCurrentDay(NowSec);

    // ---- Day rollover ----
    if (CurrentDay != LastKnownDay)
    {
        LastKnownDay = CurrentDay;

        for (auto& [Id, State] : ContextStates)
            RebuildDailySchedule(State, CurrentDay);

        // DayRolledOver fires after all schedules are rebuilt.
        BroadcastTimeEvents(NormDay, CurrentDay);
        PushSnapshotToClients();
    }

    // ---- Per-context tick ----
    for (auto& [Id, State] : ContextStates)
        TickContextState(State, NormDay, NowSec);

    // ---- Dawn / Dusk (global thresholds — per-context flags live inside FContextState) ----
    BroadcastTimeEvents(NormDay, CurrentDay);
}
```

---

## TickContextState

```cpp
void UTimeWeatherSubsystem::TickContextState(
    FContextState& State, float NormDayTime, double NowSeconds)
{
    const FWeatherBlendState PrevBlend = State.CurrentBlend;

    AdvanceBaseBlend(State, NormDayTime);
    TickOverlayEvents(State, NowSeconds);
    TickScheduledTriggers(State, NormDayTime);

    // Update season alpha continuously (transitions span multiple ticks).
    // ResolveSeasonContext is cheap — no allocation, no asset queries.
    FSeasonContext SeasonCtx = ResolveSeasonContext(State,
        TimeSnapshot.GetCurrentDay(NowSeconds));
    State.CurrentBlend.SeasonBlendAlpha = SeasonCtx.SeasonBlendAlpha;

    // Broadcast on meaningful state change (tag-level, not float-level).
    if (State.CurrentBlend.BaseWeatherA   != PrevBlend.BaseWeatherA  ||
        State.CurrentBlend.BaseWeatherB   != PrevBlend.BaseWeatherB  ||
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
    if (!KF || !KF->WeatherTag.IsValid()) return;

    FWeatherBlendState& Blend = State.CurrentBlend;

    // Keyframe changed — shift B into A, new B is the target.
    if (Blend.BaseWeatherB != KF->WeatherTag)
    {
        Blend.BaseWeatherA = Blend.BaseWeatherB.IsValid()
            ? Blend.BaseWeatherB
            : KF->WeatherTag; // first-ever assignment: A = B = target
        Blend.BaseWeatherB = KF->WeatherTag;
        Blend.BlendAlpha   = 0.f;
    }

    // Advance alpha based on real elapsed time within this keyframe.
    if (KF->TransitionDuration > 0.f)
    {
        const float ElapsedReal = (NormDayTime - KF->StartNormTime)
            * TimeSnapshot.DayDurationSeconds;
        Blend.BlendAlpha = FMath::Clamp(
            ElapsedReal / KF->TransitionDuration, 0.f, 1.f);
    }
    else
    {
        Blend.BlendAlpha = 1.f; // instant transition
    }
}
```

---

## TickOverlayEvents

```cpp
void UTimeWeatherSubsystem::TickOverlayEvents(
    FContextState& State, double NowSeconds)
{
    // Tick in reverse so we can remove by index safely.
    for (int32 i = State.ActiveEvents.Num() - 1; i >= 0; --i)
    {
        FActiveWeatherEvent& Ev = State.ActiveEvents[i];
        Ev.TickAlpha(NowSeconds);

        if (Ev.IsExpired(NowSeconds))
        {
            BroadcastEventCompleted(Ev, State);
            UnregisterFromEventRegistry(Ev);
            State.ActiveEvents.RemoveAt(i);
            ActivateQueuedEvent(State);
        }
    }

    // Write overlay into blend state. Top of ActiveEvents (index 0) = highest priority.
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

## TickScheduledTriggers

```cpp
void UTimeWeatherSubsystem::TickScheduledTriggers(
    FContextState& State, float NormDayTime)
{
    for (FScheduledEventTrigger& Trigger : State.ScheduledEventTriggers)
    {
        if (Trigger.bFired) continue;
        // Array is sorted by TriggerNormTime — break early once we pass current time.
        if (NormDayTime < Trigger.TriggerNormTime) break;

        Trigger.bFired = true;
        TriggerOverlayEvent(Trigger.Event, State.Provider);
    }
}
```

---

## ActivateQueuedEvent

Called after an active event expires. Promotes the next highest-priority queued event.

```cpp
void UTimeWeatherSubsystem::ActivateQueuedEvent(FContextState& State)
{
    if (State.QueuedEvents.IsEmpty()) return;

    // Queue is kept sorted by priority desc — first entry is the next to activate.
    FActiveWeatherEvent NewEvent = State.QueuedEvents[0];
    State.QueuedEvents.RemoveAt(0);

    // Reset timers to now (event was queued, not ticking).
    const double Now    = FPlatformTime::Seconds();
    NewEvent.StartTime  = Now;
    NewEvent.FadeInEnd  = Now + NewEvent.Definition->FadeInSeconds;
    NewEvent.SustainEnd = Now + NewEvent.Definition->FadeInSeconds
                             + NewEvent.Definition->SustainSeconds;
    NewEvent.FadeOutEnd = Now + NewEvent.Definition->TotalDurationSeconds();
    NewEvent.CurrentAlpha = 0.f;

    RegisterInEventRegistry(NewEvent);
    BroadcastEventActivated(NewEvent, State);
    State.ActiveEvents.Insert(NewEvent, 0); // new top
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

    const FGuid ContextId = GetContextId(Provider);
    FContextState* State = ContextStates.Find(ContextId);
    if (!State)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("TriggerOverlayEvent: Context not registered. Call RegisterContextProvider first."));
        return FGuid();
    }

    const int32 NewPriority = Event->Priority;

    // Determine if a higher-or-equal-priority event is already active.
    bool bShouldQueue = false;
    for (const FActiveWeatherEvent& Existing : State->ActiveEvents)
    {
        if (Existing.Priority >= NewPriority)
        { bShouldQueue = true; break; }
    }

    const double Now = FPlatformTime::Seconds();
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
        // Insert sorted by priority desc; equal priority appends (first-in wins).
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
        RegisterInEventRegistry(NewEvent);
        BroadcastEventActivated(NewEvent, *State);
        State->ActiveEvents.Insert(NewEvent, 0); // becomes new top
    }

    return NewEvent.EventId;
}
```

---

## CancelOverlayEvent

```cpp
void UTimeWeatherSubsystem::CancelOverlayEvent(FGuid EventId)
{
    if (!EventId.IsValid()) return;

    for (auto& [ContextId, State] : ContextStates)
    {
        // Check active events — begin proportional fade-out.
        for (FActiveWeatherEvent& Ev : State.ActiveEvents)
        {
            if (Ev.EventId == EventId)
            {
                const double Now = FPlatformTime::Seconds();
                // Proportional: if 60% faded in, fade out over 60% of FadeOutSeconds.
                const float Remaining = Ev.Definition->FadeOutSeconds * Ev.CurrentAlpha;
                Ev.SustainEnd = Now;
                Ev.FadeOutEnd = Now + FMath::Max(Remaining, 0.1f); // minimum 0.1s
                return;
            }
        }
        // Check queued events — just remove.
        const int32 Removed = State.QueuedEvents.RemoveAll(
            [&](const FActiveWeatherEvent& E){ return E.EventId == EventId; });
        if (Removed > 0) return;
    }
}
```

---

## RegisterInEventRegistry / UnregisterFromEventRegistry

```cpp
void UTimeWeatherSubsystem::RegisterInEventRegistry(FActiveWeatherEvent& Ev)
{
    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
    {
        Ev.RegistryHandle = Bus->RegisterActiveEvent(
            Ev.Definition->WeatherTag,
            Ev.Definition->TotalDurationSeconds(),
            Ev.Definition);
    }
}

void UTimeWeatherSubsystem::UnregisterFromEventRegistry(FActiveWeatherEvent& Ev)
{
    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
        Bus->UnregisterActiveEvent(Ev.RegistryHandle);
    Ev.RegistryHandle = FGuid(); // invalidate
}
```

---

## BroadcastTimeEvents

```cpp
void UTimeWeatherSubsystem::BroadcastTimeEvents(float NormDayTime, int32 CurrentDay)
{
    auto* Bus = UGameCoreEventSubsystem::Get(this);
    if (!Bus) return;

    // DayRolledOver — only broadcast when LastKnownDay just changed.
    // Handled in Tick before this call; use a flag to avoid duplicate fires.
    // (See Tick implementation — DayRolledOver is broadcast once per rollover.)

    // Dawn and Dusk use per-context flags.
    for (auto& [Id, State] : ContextStates)
    {
        if (!State.bDawnFired && NormDayTime >= Config->DawnThreshold)
        {
            State.bDawnFired = true;
            FTimeEvent_DawnDusk Payload;
            Payload.Day = CurrentDay;
            Payload.NormTime = NormDayTime;
            Bus->Broadcast(TAG_GameCoreEvent_Time_DawnBegan, Payload,
                EGameCoreEventScope::ServerOnly);
        }
        if (!State.bDuskFired && NormDayTime >= Config->DuskThreshold)
        {
            State.bDuskFired = true;
            FTimeEvent_DawnDusk Payload;
            Payload.Day = CurrentDay;
            Payload.NormTime = NormDayTime;
            Bus->Broadcast(TAG_GameCoreEvent_Time_DuskBegan, Payload,
                EGameCoreEventScope::ServerOnly);
        }
    }
}
```

---

## PushSnapshotToClients

```cpp
void UTimeWeatherSubsystem::PushSnapshotToClients()
{
    // AGameState must declare:
    //   UPROPERTY(Replicated) FGameTimeSnapshot TimeSnapshot;
    // and call ForceNetUpdate() here.
    if (AGameStateBase* GS = GetWorld()->GetGameState())
    {
        if (AMyGameState* MyGS = Cast<AMyGameState>(GS))
        {
            MyGS->TimeSnapshot = TimeSnapshot;
            MyGS->ForceNetUpdate();
        }
    }
}
```

**Note:** The game module is responsible for subclassing `AGameState` and adding the replicated `FGameTimeSnapshot` field. The subsystem casts to the game's `AGameState` subclass. If the project uses a generic base class, expose a `SetTimeSnapshot` method on it instead.

---

## MakeSeededStream

```cpp
FRandomStream UTimeWeatherSubsystem::MakeSeededStream(
    int32 Day, FGuid ContextId) const
{
    // Mix: base seed + day index + context GUID into a single uint32 seed.
    // HashCombine is commutative for 2 args — use a chain for non-commutativity.
    uint32 Seed = GetTypeHash(Config->WeatherSeedBase);
    Seed = HashCombine(Seed, GetTypeHash(Day));
    Seed = HashCombine(Seed, GetTypeHash(ContextId));
    return FRandomStream((int32)Seed);
}
```

---

## GetDaylightIntensity

```cpp
float UTimeWeatherSubsystem::GetDaylightIntensity(
    const IWeatherContextProvider* Provider) const
{
    const FGuid Id = GetContextId(Provider);
    const FContextState* State = ContextStates.Find(Id);
    if (!State) return 0.f;

    const float NormTime = GetNormalizedDayTime();
    FSeasonContext SeasonCtx = ResolveSeasonContext(
        const_cast<FContextState&>(*State),
        TimeSnapshot.GetCurrentDay(FPlatformTime::Seconds()));

    UCurveFloat* Curve = ResolveDayNightCurve(*State, SeasonCtx);
    return Curve ? FMath::Clamp(Curve->GetFloatValue(NormTime), 0.f, 1.f) : 0.f;
}
```

**Note:** `ResolveSeasonContext` takes a non-const ref because it stores context — a `const_cast` is acceptable here since no mutation occurs in the query path. Alternatively, split `ResolveSeasonContext` into a const variant that does not write back.
