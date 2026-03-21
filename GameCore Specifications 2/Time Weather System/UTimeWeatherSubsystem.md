# UTimeWeatherSubsystem

**Files:** `TimeWeatherSubsystem.h/.cpp`  
**Part of:** Time Weather System

`UWorldSubsystem`. Server-authority only (`ShouldCreateSubsystem` returns false on `NM_Client`). Owns time advancement, season state, daily weather schedule construction, base weather blend tracking, and overlay event management for all registered contexts.

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

    // Current normalised day time [0,1) on the server.
    float GetNormalizedDayTime() const;

    // Current blend state for the global context, or a specific region context.
    // Pass nullptr (or omit) for the global context.
    FWeatherBlendState GetCurrentWeatherState(
        const IWeatherContextProvider* Provider = nullptr) const;

    // Current season tag for a context. Returns invalid tag if no seasons defined.
    FGameplayTag GetCurrentSeasonTag(
        const IWeatherContextProvider* Provider = nullptr) const;

    // Daylight intensity [0,1] for a context at the current moment.
    // Resolution order: season curve → context default curve → 0.0 fallback.
    float GetDaylightIntensity(
        const IWeatherContextProvider* Provider = nullptr) const;

    // Region-boundary blend helper. Spatial alpha supplied by the area system.
    // 0.0 = pure A, 1.0 = pure B. Tags are winner-takes-all at 0.5 threshold.
    static FWeatherBlendState GetBlendedWeatherState(
        const FWeatherBlendState& StateA,
        const FWeatherBlendState& StateB,
        float SpatialAlpha);

    // -----------------------------------------------------------------------
    // Event API
    // -----------------------------------------------------------------------

    // Immediately trigger an overlay event on a context.
    // If a higher-or-equal-priority event is already active, this event is queued.
    // Pass nullptr for Provider to target the global context.
    // Returns FGuid() (invalid) if Event is null or context is not registered.
    FGuid TriggerOverlayEvent(
        UWeatherEventDefinition*       Event,
        const IWeatherContextProvider* Provider = nullptr);

    // Cancel an in-flight event. Triggers a proportional fade-out.
    // Safe to call with an invalid or already-expired GUID.
    void CancelOverlayEvent(FGuid EventId);

    // -----------------------------------------------------------------------
    // Region Registration
    // -----------------------------------------------------------------------

    // Call in BeginPlay (server only). Immediately allocates FContextState
    // and builds the first daily schedule.
    void RegisterContextProvider(IWeatherContextProvider* Provider);

    // Call in EndPlay (server only). Removes context state and unregisters
    // all its active events from the shared registry.
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

    // -----------------------------------------------------------------------
    // Per-Context Runtime State
    // -----------------------------------------------------------------------
    struct FContextState
    {
        UWeatherContextAsset*    Context  = nullptr;
        IWeatherContextProvider* Provider = nullptr;  // nullptr = global

        // Cached season ranges — rebuilt when context is registered.
        TArray<FSeasonRange> SeasonRanges;

        // Last season tag, used to detect season changes across ticks.
        FGameplayTag LastSeasonTag;

        // Daily weather schedule — rebuilt each day rollover.
        FDailyWeatherSchedule DailySchedule;

        // Current blend output — updated each tick.
        FWeatherBlendState CurrentBlend;

        // Active overlay events, sorted by Priority descending.
        // Only index 0 drives OverlayWeather/OverlayAlpha.
        TArray<FActiveWeatherEvent> ActiveEvents;

        // Events waiting for the active slot. Sorted by Priority descending;
        // equal-priority events ordered by insertion time (first-in wins).
        TArray<FActiveWeatherEvent> QueuedEvents;

        // Probabilistic events scheduled for today. Sorted by TriggerNormTime.
        TArray<FScheduledEventTrigger> ScheduledEventTriggers;

        // Per-context dawn/dusk fired flags; reset on day rollover.
        bool bDawnFired = false;
        bool bDuskFired = false;
    };

    // Keyed by IWeatherContextProvider::GetContextId().
    // FGuid() (null GUID) = global context.
    TMap<FGuid, FContextState> ContextStates;

    // Raw interface pointers — safe because we own lifetime via Register/Unregister.
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

    FSeasonContext     ResolveSeasonContext(FContextState& State, int32 Day) const;
    USeasonDefinition* ResolveActiveSeason(const FContextState& State,
                                           const FSeasonContext& Ctx) const;
    UWeatherSequence*  ResolveSequence(const FContextState& State,
                                       const FSeasonContext& Ctx) const;
    UCurveFloat*       ResolveDayNightCurve(const FContextState& State,
                                           const FSeasonContext& Ctx) const;
    FRandomStream      MakeSeededStream(int32 Day, FGuid ContextId) const;

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
    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
    {
        for (auto& [Id, State] : ContextStates)
            for (FActiveWeatherEvent& Ev : State.ActiveEvents)
                Bus->UnregisterActiveEvent(Ev.RegistryHandle);
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
            TEXT("UTimeWeatherSubsystem: Context GUID already registered. Duplicate ignored."));
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
        if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
            for (FActiveWeatherEvent& Ev : State->ActiveEvents)
                Bus->UnregisterActiveEvent(Ev.RegistryHandle);
        ContextStates.Remove(Id);
    }
    RegisteredProviders.Remove(Provider);
}
```

---

## RebuildContextCaches

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

```cpp
FSeasonContext UTimeWeatherSubsystem::ResolveSeasonContext(
    FContextState& State, int32 Day) const
{
    FSeasonContext Ctx;
    Ctx.CurrentDay       = Day;
    Ctx.DayOfYear        = Day % FMath::Max(1, TimeSnapshot.YearLengthDays);
    Ctx.NormalizedDayTime = 0.f;
    Ctx.LastWeather      = State.CurrentBlend.BaseWeatherB;

    if (State.SeasonRanges.IsEmpty()) return Ctx;

    for (int32 i = 0; i < State.SeasonRanges.Num(); ++i)
    {
        const FSeasonRange& R = State.SeasonRanges[i];
        if (R.ContainsDayOfYear(Ctx.DayOfYear))
        {
            Ctx.CurrentSeason = R.Season ? R.Season->SeasonTag : FGameplayTag();

            const int32 PrevIdx = (i == 0)
                ? State.SeasonRanges.Num() - 1
                : i - 1;
            const FSeasonRange& Prev = State.SeasonRanges[PrevIdx];
            Ctx.PreviousSeason = Prev.Season ? Prev.Season->SeasonTag : FGameplayTag();

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

## Resolve Helpers

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

FRandomStream UTimeWeatherSubsystem::MakeSeededStream(
    int32 Day, FGuid ContextId) const
{
    uint32 Seed = GetTypeHash(Config->WeatherSeedBase);
    Seed = HashCombine(Seed, GetTypeHash(Day));
    Seed = HashCombine(Seed, GetTypeHash(ContextId));
    return FRandomStream((int32)Seed);
}
```

---

## RebuildDailySchedule

```cpp
void UTimeWeatherSubsystem::RebuildDailySchedule(
    FContextState& State, int32 Day)
{
    State.DailySchedule.Reset();
    State.ScheduledEventTriggers.Reset();
    if (!State.Context) return;

    FSeasonContext SeasonCtx = ResolveSeasonContext(State, Day);

    if (SeasonCtx.CurrentSeason != State.LastSeasonTag)
    {
        BroadcastSeasonChanged(State, State.LastSeasonTag, SeasonCtx.CurrentSeason);
        State.LastSeasonTag = SeasonCtx.CurrentSeason;
        State.CurrentBlend.CurrentSeason    = SeasonCtx.CurrentSeason;
        State.CurrentBlend.PreviousSeason   = SeasonCtx.PreviousSeason;
        State.CurrentBlend.SeasonBlendAlpha = SeasonCtx.SeasonBlendAlpha;
    }

    UWeatherSequence* Sequence = ResolveSequence(State, SeasonCtx);
    if (!Sequence)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("UTimeWeatherSubsystem: No weather sequence resolved. Schedule empty."));
        return;
    }

    const FGuid ContextId  = GetContextId(State.Provider);
    FRandomStream Stream   = MakeSeededStream(Day, ContextId);
    const int32  NumKF     = Sequence->GetKeyframesPerDay();
    const float  SlotSize  = 1.f / (float)FMath::Max(1, NumKF);
    FGameplayTag Current   = State.CurrentBlend.BaseWeatherB;

    for (int32 i = 0; i < NumKF; ++i)
    {
        float TransitionSec = 0.f;
        FGameplayTag Next = Sequence->GetNextWeather(Current, SeasonCtx, Stream, TransitionSec);

        FDailyWeatherKeyframe& KF = State.DailySchedule.Keyframes.AddDefaulted_GetRef();
        KF.StartNormTime      = i * SlotSize;
        KF.EndNormTime        = (i + 1) * SlotSize;
        KF.WeatherTag         = Next;
        KF.TransitionDuration = FMath::Clamp(TransitionSec, 0.f,
            SlotSize * TimeSnapshot.DayDurationSeconds);
        Current = Next;
    }

    // Roll probabilistic events using the same stream (order-dependent).
    RollTimedEvents(State, SeasonCtx, Stream, Day);

    State.bDawnFired = false;
    State.bDuskFired = false;
}
```

---

## RollTimedEvents

```cpp
void UTimeWeatherSubsystem::RollTimedEvents(
    FContextState& State, const FSeasonContext& SeasonCtx,
    FRandomStream& Stream, int32 Day)
{
    USeasonDefinition* Season = ResolveActiveSeason(State, SeasonCtx);
    if (!Season) return;

    for (const FSeasonWeatherEvent& SE : Season->TimedEvents)
    {
        if (!SE.Event) continue;
        if (SE.WindowEnd <= SE.WindowStart) continue;
        if (Stream.GetFraction() > SE.DailyProbability) continue;

        float TriggerNorm = Stream.FRandRange(SE.WindowStart, SE.WindowEnd);
        FScheduledEventTrigger& Trigger = State.ScheduledEventTriggers.AddDefaulted_GetRef();
        Trigger.Event           = SE.Event;
        Trigger.TriggerNormTime = TriggerNorm;
        Trigger.bFired          = false;
    }

    // Sort ascending so TickScheduledTriggers can break early.
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

    const double NowSec     = FPlatformTime::Seconds();
    const float  NormDay    = TimeSnapshot.GetNormalizedDayTime(NowSec);
    const int32  CurrentDay = TimeSnapshot.GetCurrentDay(NowSec);

    if (CurrentDay != LastKnownDay)
    {
        LastKnownDay = CurrentDay;
        for (auto& [Id, State] : ContextStates)
            RebuildDailySchedule(State, CurrentDay);

        if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
        {
            FTimeEvent_DayRolledOver Payload;
            Payload.NewDay    = CurrentDay;
            Payload.DayOfYear = CurrentDay % FMath::Max(1, TimeSnapshot.YearLengthDays);
            Bus->Broadcast(TAG_GameCoreEvent_Time_DayRolledOver, Payload,
                EGameCoreEventScope::ServerOnly);
        }
        PushSnapshotToClients();
    }

    for (auto& [Id, State] : ContextStates)
        TickContextState(State, NormDay, NowSec);

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

    // Continuous season alpha update (transitions span multiple ticks).
    FSeasonContext SeasonCtx = ResolveSeasonContext(State,
        TimeSnapshot.GetCurrentDay(NowSeconds));
    State.CurrentBlend.SeasonBlendAlpha = SeasonCtx.SeasonBlendAlpha;

    // Broadcast on tag-level change only.
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

    if (Blend.BaseWeatherB != KF->WeatherTag)
    {
        Blend.BaseWeatherA = Blend.BaseWeatherB.IsValid()
            ? Blend.BaseWeatherB
            : KF->WeatherTag;
        Blend.BaseWeatherB = KF->WeatherTag;
        Blend.BlendAlpha   = 0.f;
    }

    if (KF->TransitionDuration > 0.f)
    {
        const float ElapsedReal = (NormDayTime - KF->StartNormTime)
            * TimeSnapshot.DayDurationSeconds;
        Blend.BlendAlpha = FMath::Clamp(
            ElapsedReal / KF->TransitionDuration, 0.f, 1.f);
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
        if (NormDayTime < Trigger.TriggerNormTime) break; // sorted
        Trigger.bFired = true;
        TriggerOverlayEvent(Trigger.Event, State.Provider);
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

    const FGuid ContextId = GetContextId(Provider);
    FContextState* State = ContextStates.Find(ContextId);
    if (!State)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("TriggerOverlayEvent: Context not registered."));
        return FGuid();
    }

    const int32 NewPriority = Event->Priority;
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
        State->ActiveEvents.Insert(NewEvent, 0);
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
        for (FActiveWeatherEvent& Ev : State.ActiveEvents)
        {
            if (Ev.EventId == EventId)
            {
                const double Now     = FPlatformTime::Seconds();
                const float Remaining = Ev.Definition->FadeOutSeconds * Ev.CurrentAlpha;
                Ev.SustainEnd = Now;
                Ev.FadeOutEnd = Now + FMath::Max(Remaining, 0.1f);
                return;
            }
        }
        const int32 Removed = State.QueuedEvents.RemoveAll(
            [&](const FActiveWeatherEvent& E){ return E.EventId == EventId; });
        if (Removed > 0) return;
    }
}
```

---

## ActivateQueuedEvent

```cpp
void UTimeWeatherSubsystem::ActivateQueuedEvent(FContextState& State)
{
    if (State.QueuedEvents.IsEmpty()) return;

    FActiveWeatherEvent NewEvent = State.QueuedEvents[0];
    State.QueuedEvents.RemoveAt(0);

    const double Now    = FPlatformTime::Seconds();
    NewEvent.StartTime  = Now;
    NewEvent.FadeInEnd  = Now + NewEvent.Definition->FadeInSeconds;
    NewEvent.SustainEnd = Now + NewEvent.Definition->FadeInSeconds
                             + NewEvent.Definition->SustainSeconds;
    NewEvent.FadeOutEnd = Now + NewEvent.Definition->TotalDurationSeconds();
    NewEvent.CurrentAlpha = 0.f;

    RegisterInEventRegistry(NewEvent);
    BroadcastEventActivated(NewEvent, State);
    State.ActiveEvents.Insert(NewEvent, 0);
}
```

---

## Registry and Broadcast Helpers

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
    Ev.RegistryHandle = FGuid();
}

void UTimeWeatherSubsystem::BroadcastWeatherChanged(const FContextState& State)
{
    auto* Bus = UGameCoreEventSubsystem::Get(this);
    if (!Bus) return;
    FWeatherEvent_StateChanged Payload;
    Payload.NewState  = State.CurrentBlend;
    Payload.ContextId = State.Provider ? State.Provider->GetContextId() : FGuid();
    Bus->Broadcast(TAG_GameCoreEvent_Weather_StateChanged, Payload,
        EGameCoreEventScope::ServerOnly);
}

void UTimeWeatherSubsystem::BroadcastSeasonChanged(
    const FContextState& State, FGameplayTag OldSeason, FGameplayTag NewSeason)
{
    auto* Bus = UGameCoreEventSubsystem::Get(this);
    if (!Bus) return;
    FTimeEvent_SeasonChanged Payload;
    Payload.PreviousSeason = OldSeason;
    Payload.NewSeason      = NewSeason;
    Payload.ContextId      = State.Provider ? State.Provider->GetContextId() : FGuid();
    Bus->Broadcast(TAG_GameCoreEvent_Time_SeasonChanged, Payload,
        EGameCoreEventScope::ServerOnly);
}

void UTimeWeatherSubsystem::BroadcastEventActivated(
    const FActiveWeatherEvent& Ev, const FContextState& State)
{
    auto* Bus = UGameCoreEventSubsystem::Get(this);
    if (!Bus) return;
    FWeatherEvent_EventActivated Payload;
    Payload.EventWeatherTag = Ev.Definition->WeatherTag;
    Payload.Priority        = Ev.Priority;
    Payload.FadeInSeconds   = Ev.Definition->FadeInSeconds;
    Payload.SustainSeconds  = Ev.Definition->SustainSeconds;
    Payload.FadeOutSeconds  = Ev.Definition->FadeOutSeconds;
    Payload.EventId         = Ev.EventId;
    Payload.ContextId       = State.Provider ? State.Provider->GetContextId() : FGuid();
    Bus->Broadcast(TAG_GameCoreEvent_Weather_EventActivated, Payload,
        EGameCoreEventScope::ServerOnly);
}

void UTimeWeatherSubsystem::BroadcastEventCompleted(
    const FActiveWeatherEvent& Ev, const FContextState& State)
{
    auto* Bus = UGameCoreEventSubsystem::Get(this);
    if (!Bus) return;
    FWeatherEvent_EventCompleted Payload;
    Payload.EventWeatherTag = Ev.Definition->WeatherTag;
    Payload.EventId         = Ev.EventId;
    Payload.ContextId       = State.Provider ? State.Provider->GetContextId() : FGuid();
    Bus->Broadcast(TAG_GameCoreEvent_Weather_EventCompleted, Payload,
        EGameCoreEventScope::ServerOnly);
}

void UTimeWeatherSubsystem::BroadcastTimeEvents(
    float NormDayTime, int32 CurrentDay)
{
    auto* Bus = UGameCoreEventSubsystem::Get(this);
    if (!Bus) return;

    for (auto& [Id, State] : ContextStates)
    {
        if (!State.bDawnFired && NormDayTime >= Config->DawnThreshold)
        {
            State.bDawnFired = true;
            FTimeEvent_DawnDusk Payload;
            Payload.Day       = CurrentDay;
            Payload.NormTime  = NormDayTime;
            Payload.ContextId = State.Provider ? State.Provider->GetContextId() : FGuid();
            Bus->Broadcast(TAG_GameCoreEvent_Time_DawnBegan, Payload,
                EGameCoreEventScope::ServerOnly);
        }
        if (!State.bDuskFired && NormDayTime >= Config->DuskThreshold)
        {
            State.bDuskFired = true;
            FTimeEvent_DawnDusk Payload;
            Payload.Day       = CurrentDay;
            Payload.NormTime  = NormDayTime;
            Payload.ContextId = State.Provider ? State.Provider->GetContextId() : FGuid();
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
    // AMyGameState must be the game module's AGameState subclass with:
    //   UPROPERTY(Replicated) FGameTimeSnapshot TimeSnapshot;
    // The subsystem casts to it and calls ForceNetUpdate.
    // If the project uses a different name, expose SetTimeSnapshot() instead.
    if (AMyGameState* GS = GetWorld()->GetGameState<AMyGameState>())
    {
        GS->TimeSnapshot = TimeSnapshot;
        GS->ForceNetUpdate();
    }
}
```

**Coupling note:** This method hard-couples the plugin to `AMyGameState`. The recommended pattern is to expose a `SetTimeSnapshot` interface on a base class or to move this call into the game module via a delegate. See Code Review for discussion.

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
    // const_cast: ResolveSeasonContext is logically const; no mutation occurs.
    FSeasonContext SeasonCtx = ResolveSeasonContext(
        const_cast<FContextState&>(*State),
        TimeSnapshot.GetCurrentDay(FPlatformTime::Seconds()));

    UCurveFloat* Curve = ResolveDayNightCurve(*State, SeasonCtx);
    return Curve ? FMath::Clamp(Curve->GetFloatValue(NormTime), 0.f, 1.f) : 0.f;
}
```

---

## GetBlendedWeatherState (Static)

```cpp
// static
FWeatherBlendState UTimeWeatherSubsystem::GetBlendedWeatherState(
    const FWeatherBlendState& A, const FWeatherBlendState& B, float Alpha)
{
    // Tags are winner-takes-all at 0.5 threshold.
    // The area system is expected to supply a smooth alpha so hard flips are rare.
    FWeatherBlendState Result;
    const FWeatherBlendState& Dominant = (Alpha < 0.5f) ? A : B;

    Result.BaseWeatherA     = Dominant.BaseWeatherA;
    Result.BaseWeatherB     = Dominant.BaseWeatherB;
    Result.BlendAlpha       = FMath::Lerp(A.BlendAlpha, B.BlendAlpha, Alpha);
    Result.OverlayWeather   = Dominant.OverlayWeather;
    Result.OverlayAlpha     = FMath::Lerp(A.OverlayAlpha, B.OverlayAlpha, Alpha);
    Result.CurrentSeason    = Dominant.CurrentSeason;
    Result.PreviousSeason   = Dominant.PreviousSeason;
    Result.SeasonBlendAlpha = FMath::Lerp(A.SeasonBlendAlpha, B.SeasonBlendAlpha, Alpha);
    return Result;
}
```
