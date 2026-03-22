#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TimeWeather/TimeWeatherTypes.h"
#include "TimeWeatherEventMessages.generated.h"

// =============================================================================
// Channel Tags — declared here, defined in TimeWeatherEventMessages.cpp
// =============================================================================

// GameCoreEvent.Time.DawnBegan  — fires once per day per context at DawnThreshold
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Time_DawnBegan)
// GameCoreEvent.Time.DuskBegan  — fires once per day per context at DuskThreshold
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Time_DuskBegan)
// GameCoreEvent.Time.DayRolledOver  — fires when the in-game day index increments
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Time_DayRolledOver)
// GameCoreEvent.Time.SeasonChanged  — fires when the resolved season tag changes
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Time_SeasonChanged)
// GameCoreEvent.Weather.StateChanged  — fires when BaseWeatherA/B or OverlayWeather changes
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Weather_StateChanged)
// GameCoreEvent.Weather.EventActivated  — fires when an overlay event becomes top-priority
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Weather_EventActivated)
// GameCoreEvent.Weather.EventCompleted  — fires when an overlay event fully fades out
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Weather_EventCompleted)

// =============================================================================
// Payload Structs
// =============================================================================

/** Payload for DawnBegan and DuskBegan. */
USTRUCT()
struct FTimeEvent_DawnDusk
{
    GENERATED_BODY()
    int32 Day      = 0;    // current in-game day index
    float NormTime = 0.f;  // normalised day time when fired
    FGuid ContextId;        // FGuid() = global context
};

/** Payload for DayRolledOver. */
USTRUCT()
struct FTimeEvent_DayRolledOver
{
    GENERATED_BODY()
    int32 NewDay    = 0;
    int32 DayOfYear = 0;   // NewDay % YearLengthDays
};

/** Payload for SeasonChanged. */
USTRUCT()
struct FTimeEvent_SeasonChanged
{
    GENERATED_BODY()
    FGameplayTag PreviousSeason;
    FGameplayTag NewSeason;
    FGuid        ContextId;   // FGuid() = global
};

/** Payload for Weather.StateChanged. Carries the full new state — subscribers do not need to re-query. */
USTRUCT()
struct FWeatherEvent_StateChanged
{
    GENERATED_BODY()
    FWeatherBlendState NewState;
    FGuid              ContextId;   // FGuid() = global
};

/** Payload for Weather.EventActivated. */
USTRUCT()
struct FWeatherEvent_EventActivated
{
    GENERATED_BODY()
    FGameplayTag EventWeatherTag;
    int32        Priority       = 0;
    float        FadeInSeconds  = 0.f;
    float        SustainSeconds = 0.f;
    float        FadeOutSeconds = 0.f;
    FGuid        EventId;        // cancellation handle returned by TriggerOverlayEvent
    FGuid        ContextId;      // FGuid() = global
};

/** Payload for Weather.EventCompleted. */
USTRUCT()
struct FWeatherEvent_EventCompleted
{
    GENERATED_BODY()
    FGameplayTag EventWeatherTag;
    FGuid        EventId;
    FGuid        ContextId;
};
