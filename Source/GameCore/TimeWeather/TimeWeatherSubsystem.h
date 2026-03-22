#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "TimeWeather/TimeWeatherTypes.h"
#include "TimeWeather/WeatherContextProvider.h"
#include "TimeWeatherSubsystem.generated.h"

class UTimeWeatherConfig;
class UWeatherContextAsset;
class USeasonDefinition;
class UWeatherSequence;
class UCurveFloat;

/**
 * UTimeWeatherSubsystem
 *
 * Server-authority only (ShouldCreateSubsystem returns false on NM_Client).
 * Owns time advancement, season state, daily weather schedule construction,
 * base weather blend tracking, and overlay event management for all registered contexts.
 *
 * Global context (FGuid()) is always present after Initialize.
 * Region contexts are registered via RegisterContextProvider from BeginPlay.
 */
UCLASS()
class GAMECORE_API UTimeWeatherSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    // Public Query API
    // -----------------------------------------------------------------------

    /** Current time snapshot (stateless config). Safe to call from any server system. */
    const FGameTimeSnapshot& GetTimeSnapshot() const { return TimeSnapshot; }

    /** Current normalised day time [0,1) on the server. */
    float GetNormalizedDayTime() const;

    /**
     * Current blend state for the global context, or a specific region context.
     * Pass nullptr (or omit) for the global context.
     */
    FWeatherBlendState GetCurrentWeatherState(
        const IWeatherContextProvider* Provider = nullptr) const;

    /** Current season tag for a context. Returns invalid tag if no seasons defined. */
    FGameplayTag GetCurrentSeasonTag(
        const IWeatherContextProvider* Provider = nullptr) const;

    /**
     * Daylight intensity [0,1] for a context at the current moment.
     * Resolution order: season curve → context default curve → 0.0 fallback.
     */
    float GetDaylightIntensity(
        const IWeatherContextProvider* Provider = nullptr) const;

    /**
     * Region-boundary blend helper. Spatial alpha supplied by the area system.
     * 0.0 = pure A, 1.0 = pure B. Tags are winner-takes-all at 0.5 threshold.
     */
    static FWeatherBlendState GetBlendedWeatherState(
        const FWeatherBlendState& StateA,
        const FWeatherBlendState& StateB,
        float SpatialAlpha);

    // -----------------------------------------------------------------------
    // Event API
    // -----------------------------------------------------------------------

    /**
     * Immediately trigger an overlay event on a context.
     * If a higher-or-equal-priority event is already active, this event is queued.
     * Pass nullptr for Provider to target the global context.
     * Returns FGuid() (invalid) if Event is null or context is not registered.
     */
    FGuid TriggerOverlayEvent(
        UWeatherEventDefinition*       Event,
        const IWeatherContextProvider* Provider = nullptr);

    /**
     * Cancel an in-flight event. Triggers a proportional fade-out.
     * Safe to call with an invalid or already-expired GUID.
     */
    void CancelOverlayEvent(FGuid EventId);

    // -----------------------------------------------------------------------
    // Region Registration
    // -----------------------------------------------------------------------

    /** Call in BeginPlay (server only). Allocates FContextState and builds first daily schedule. */
    void RegisterContextProvider(IWeatherContextProvider* Provider);

    /** Call in EndPlay (server only). Removes context state and unregisters all active events. */
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

        /** Cached season ranges — rebuilt when context is registered. */
        TArray<FSeasonRange> SeasonRanges;

        /** Last season tag, used to detect season changes across ticks. */
        FGameplayTag LastSeasonTag;

        /** Daily weather schedule — rebuilt each day rollover. */
        FDailyWeatherSchedule DailySchedule;

        /** Current blend output — updated each tick. */
        FWeatherBlendState CurrentBlend;

        /**
         * Active overlay events, sorted by Priority descending.
         * Only index 0 drives OverlayWeather/OverlayAlpha.
         */
        TArray<FActiveWeatherEvent> ActiveEvents;

        /**
         * Events waiting for the active slot. Sorted by Priority descending;
         * equal-priority events ordered by insertion time (first-in wins).
         */
        TArray<FActiveWeatherEvent> QueuedEvents;

        /** Probabilistic events scheduled for today. Sorted by TriggerNormTime. */
        TArray<FScheduledEventTrigger> ScheduledEventTriggers;

        /** Per-context dawn/dusk fired flags; reset on day rollover. */
        bool bDawnFired = false;
        bool bDuskFired = false;
    };

    /** Keyed by IWeatherContextProvider::GetContextId(). FGuid() (null) = global context. */
    TMap<FGuid, FContextState> ContextStates;

    /** Raw interface pointers — safe because we own lifetime via Register/Unregister. */
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
