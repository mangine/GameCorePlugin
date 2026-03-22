#include "TimeWeatherSubsystem.h"
#include "GameCore.h"
#include "EventBus/GameCoreEventBus.h"
#include "TimeWeather/TimeWeatherConfig.h"
#include "TimeWeather/TimeWeatherEventMessages.h"
#include "TimeWeather/WeatherContextAsset.h"
#include "TimeWeather/WeatherEventDefinition.h"
#include "TimeWeather/SeasonDefinition.h"
#include "TimeWeather/WeatherSequence.h"
#include "Curves/CurveFloat.h"

// =============================================================================
// UWorldSubsystem overrides
// =============================================================================

bool UTimeWeatherSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    if (!Super::ShouldCreateSubsystem(Outer)) return false;
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->GetNetMode() != NM_Client;
}

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

void UTimeWeatherSubsystem::Deinitialize()
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
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

        if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        {
            FTimeEvent_DayRolledOver Payload;
            Payload.NewDay    = CurrentDay;
            Payload.DayOfYear = CurrentDay % FMath::Max(1, TimeSnapshot.YearLengthDays);
            Bus->Broadcast(TAG_GameCoreEvent_Time_DayRolledOver,
                FInstancedStruct::Make(Payload),
                EGameCoreEventScope::ServerOnly);
        }
        PushSnapshotToClients();
    }

    for (auto& [Id, State] : ContextStates)
        TickContextState(State, NormDay, NowSec);

    BroadcastTimeEvents(NormDay, CurrentDay);
}

TStatId UTimeWeatherSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeWeatherSubsystem, STATGROUP_Tickables);
}

// =============================================================================
// Public Query API
// =============================================================================

float UTimeWeatherSubsystem::GetNormalizedDayTime() const
{
    return TimeSnapshot.GetNormalizedDayTime(FPlatformTime::Seconds());
}

FWeatherBlendState UTimeWeatherSubsystem::GetCurrentWeatherState(
    const IWeatherContextProvider* Provider) const
{
    const FGuid Id = GetContextId(Provider);
    if (const FContextState* State = ContextStates.Find(Id))
        return State->CurrentBlend;
    return FWeatherBlendState{};
}

FGameplayTag UTimeWeatherSubsystem::GetCurrentSeasonTag(
    const IWeatherContextProvider* Provider) const
{
    const FGuid Id = GetContextId(Provider);
    if (const FContextState* State = ContextStates.Find(Id))
        return State->CurrentBlend.CurrentSeason;
    return FGameplayTag{};
}

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

// static
FWeatherBlendState UTimeWeatherSubsystem::GetBlendedWeatherState(
    const FWeatherBlendState& A, const FWeatherBlendState& B, float Alpha)
{
    // Tags are winner-takes-all at 0.5 threshold.
    const FWeatherBlendState& Dominant = (Alpha < 0.5f) ? A : B;

    FWeatherBlendState Result;
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

// =============================================================================
// Event API
// =============================================================================

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
            TEXT("UTimeWeatherSubsystem::TriggerOverlayEvent: Context not registered."));
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

void UTimeWeatherSubsystem::CancelOverlayEvent(FGuid EventId)
{
    if (!EventId.IsValid()) return;

    for (auto& [ContextId, State] : ContextStates)
    {
        for (FActiveWeatherEvent& Ev : State.ActiveEvents)
        {
            if (Ev.EventId == EventId)
            {
                const double Now      = FPlatformTime::Seconds();
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

// =============================================================================
// Region Registration
// =============================================================================

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
        if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
            for (FActiveWeatherEvent& Ev : State->ActiveEvents)
                Bus->UnregisterActiveEvent(Ev.RegistryHandle);
        ContextStates.Remove(Id);
    }
    RegisteredProviders.Remove(Provider);
}

// =============================================================================
// Internal — Initialization
// =============================================================================

void UTimeWeatherSubsystem::InitGlobalContext()
{
    FContextState& Global = ContextStates.FindOrAdd(FGuid());
    Global.Context  = Config->GlobalContext;
    Global.Provider = nullptr;
    RebuildContextCaches(Global);
    RebuildDailySchedule(Global, TimeSnapshot.GetCurrentDay(FPlatformTime::Seconds()));
}

void UTimeWeatherSubsystem::RebuildContextCaches(FContextState& State)
{
    State.SeasonRanges.Reset();
    if (State.Context)
        State.Context->ComputeSeasonRanges(TimeSnapshot.YearLengthDays, State.SeasonRanges);
}

// =============================================================================
// Internal — Schedule Building
// =============================================================================

void UTimeWeatherSubsystem::RebuildDailySchedule(FContextState& State, int32 Day)
{
    State.DailySchedule.Reset();
    State.ScheduledEventTriggers.Reset();
    if (!State.Context) return;

    FSeasonContext SeasonCtx = ResolveSeasonContext(State, Day);

    if (SeasonCtx.CurrentSeason != State.LastSeasonTag)
    {
        BroadcastSeasonChanged(State, State.LastSeasonTag, SeasonCtx.CurrentSeason);
        State.LastSeasonTag                 = SeasonCtx.CurrentSeason;
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

    State.ScheduledEventTriggers.Sort(
        [](const FScheduledEventTrigger& A, const FScheduledEventTrigger& B)
        { return A.TriggerNormTime < B.TriggerNormTime; });
}

// =============================================================================
// Internal — Per-Tick
// =============================================================================

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

    // Broadcast on tag-level change only (never on alpha-only changes).
    if (State.CurrentBlend.BaseWeatherA   != PrevBlend.BaseWeatherA  ||
        State.CurrentBlend.BaseWeatherB   != PrevBlend.BaseWeatherB  ||
        State.CurrentBlend.OverlayWeather != PrevBlend.OverlayWeather)
    {
        BroadcastWeatherChanged(State);
    }
}

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

void UTimeWeatherSubsystem::TickScheduledTriggers(
    FContextState& State, float NormDayTime)
{
    for (FScheduledEventTrigger& Trigger : State.ScheduledEventTriggers)
    {
        if (Trigger.bFired) continue;
        if (NormDayTime < Trigger.TriggerNormTime) break; // sorted — early exit
        Trigger.bFired = true;
        TriggerOverlayEvent(Trigger.Event, State.Provider);
    }
}

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

// =============================================================================
// Internal — Registry & Broadcast
// =============================================================================

void UTimeWeatherSubsystem::RegisterInEventRegistry(FActiveWeatherEvent& Ev)
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        Ev.RegistryHandle = Bus->RegisterActiveEvent(
            Ev.Definition->WeatherTag,
            Ev.Definition->TotalDurationSeconds(),
            Ev.Definition);
    }
}

void UTimeWeatherSubsystem::UnregisterFromEventRegistry(FActiveWeatherEvent& Ev)
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        Bus->UnregisterActiveEvent(Ev.RegistryHandle);
    Ev.RegistryHandle = FGuid();
}

void UTimeWeatherSubsystem::BroadcastWeatherChanged(const FContextState& State)
{
    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (!Bus) return;
    FWeatherEvent_StateChanged Payload;
    Payload.NewState  = State.CurrentBlend;
    Payload.ContextId = State.Provider ? State.Provider->GetContextId() : FGuid();
    Bus->Broadcast(TAG_GameCoreEvent_Weather_StateChanged,
        FInstancedStruct::Make(Payload),
        EGameCoreEventScope::ServerOnly);
}

void UTimeWeatherSubsystem::BroadcastSeasonChanged(
    const FContextState& State, FGameplayTag OldSeason, FGameplayTag NewSeason)
{
    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (!Bus) return;
    FTimeEvent_SeasonChanged Payload;
    Payload.PreviousSeason = OldSeason;
    Payload.NewSeason      = NewSeason;
    Payload.ContextId      = State.Provider ? State.Provider->GetContextId() : FGuid();
    Bus->Broadcast(TAG_GameCoreEvent_Time_SeasonChanged,
        FInstancedStruct::Make(Payload),
        EGameCoreEventScope::ServerOnly);
}

void UTimeWeatherSubsystem::BroadcastEventActivated(
    const FActiveWeatherEvent& Ev, const FContextState& State)
{
    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (!Bus) return;
    FWeatherEvent_EventActivated Payload;
    Payload.EventWeatherTag = Ev.Definition->WeatherTag;
    Payload.Priority        = Ev.Priority;
    Payload.FadeInSeconds   = Ev.Definition->FadeInSeconds;
    Payload.SustainSeconds  = Ev.Definition->SustainSeconds;
    Payload.FadeOutSeconds  = Ev.Definition->FadeOutSeconds;
    Payload.EventId         = Ev.EventId;
    Payload.ContextId       = State.Provider ? State.Provider->GetContextId() : FGuid();
    Bus->Broadcast(TAG_GameCoreEvent_Weather_EventActivated,
        FInstancedStruct::Make(Payload),
        EGameCoreEventScope::ServerOnly);
}

void UTimeWeatherSubsystem::BroadcastEventCompleted(
    const FActiveWeatherEvent& Ev, const FContextState& State)
{
    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (!Bus) return;
    FWeatherEvent_EventCompleted Payload;
    Payload.EventWeatherTag = Ev.Definition->WeatherTag;
    Payload.EventId         = Ev.EventId;
    Payload.ContextId       = State.Provider ? State.Provider->GetContextId() : FGuid();
    Bus->Broadcast(TAG_GameCoreEvent_Weather_EventCompleted,
        FInstancedStruct::Make(Payload),
        EGameCoreEventScope::ServerOnly);
}

void UTimeWeatherSubsystem::BroadcastTimeEvents(float NormDayTime, int32 CurrentDay)
{
    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (!Bus) return;

    for (auto& [Id, State] : ContextStates)
    {
        if (!State.bDawnFired && NormDayTime >= Config->DawnThreshold)
        {
            State.bDawnFired = true;
            FTimeEvent_DawnDusk Payload;
            Payload.Day      = CurrentDay;
            Payload.NormTime = NormDayTime;
            Payload.ContextId = State.Provider ? State.Provider->GetContextId() : FGuid();
            Bus->Broadcast(TAG_GameCoreEvent_Time_DawnBegan,
                FInstancedStruct::Make(Payload),
                EGameCoreEventScope::ServerOnly);
        }
        if (!State.bDuskFired && NormDayTime >= Config->DuskThreshold)
        {
            State.bDuskFired = true;
            FTimeEvent_DawnDusk Payload;
            Payload.Day      = CurrentDay;
            Payload.NormTime = NormDayTime;
            Payload.ContextId = State.Provider ? State.Provider->GetContextId() : FGuid();
            Bus->Broadcast(TAG_GameCoreEvent_Time_DuskBegan,
                FInstancedStruct::Make(Payload),
                EGameCoreEventScope::ServerOnly);
        }
    }
}

void UTimeWeatherSubsystem::PushSnapshotToClients()
{
    // DEV-3: The spec couples this to AMyGameState which is game-project-specific.
    // Projects should expose a delegate here or override PushSnapshotToClients in a
    // game-module subclass. The default implementation is intentionally a no-op stub.
    // See GameCore Specifications 2/Time Weather System/DEVIATIONS.md — DEV-3.
}

// =============================================================================
// Internal — Resolve Helpers
// =============================================================================

FSeasonContext UTimeWeatherSubsystem::ResolveSeasonContext(
    FContextState& State, int32 Day) const
{
    FSeasonContext Ctx;
    Ctx.CurrentDay        = Day;
    Ctx.DayOfYear         = Day % FMath::Max(1, TimeSnapshot.YearLengthDays);
    Ctx.NormalizedDayTime = 0.f;
    Ctx.LastWeather       = State.CurrentBlend.BaseWeatherB;

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
                float DayWithin      = R.GetDayWithinSeason(Ctx.DayOfYear);
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
