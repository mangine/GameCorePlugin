#pragma once
#include "CoreMinimal.h"
#include "QuestEnums.generated.h"

UENUM(BlueprintType)
enum class EQuestLifecycle : uint8
{
    SingleAttempt            UMETA(DisplayName = "Single Attempt"),
    RetryUntilComplete       UMETA(DisplayName = "Retry Until Complete"),
    RetryAndAssist           UMETA(DisplayName = "Retry and Assist"),
    Evergreen                UMETA(DisplayName = "Evergreen"),
};

UENUM(BlueprintType)
enum class EQuestCheckAuthority : uint8
{
    ServerAuthoritative      UMETA(DisplayName = "Server Authoritative"),
    ClientValidated          UMETA(DisplayName = "Client Validated"),
};

UENUM(BlueprintType)
enum class EQuestResetCadence : uint8
{
    None                     UMETA(DisplayName = "None (Fixed Cooldown)"),
    Daily                    UMETA(DisplayName = "Daily (00:00 UTC)"),
    Weekly                   UMETA(DisplayName = "Weekly (Monday 00:00 UTC)"),
    EventBound               UMETA(DisplayName = "Event Bound"),
};

UENUM(BlueprintType)
enum class EQuestMemberRole : uint8
{
    Primary                  UMETA(DisplayName = "Primary"),
    Helper                   UMETA(DisplayName = "Helper"),
};

UENUM(BlueprintType)
enum class EQuestDifficulty : uint8
{
    Trivial     UMETA(DisplayName = "Trivial"),
    Easy        UMETA(DisplayName = "Easy"),
    Normal      UMETA(DisplayName = "Normal"),
    Hard        UMETA(DisplayName = "Hard"),
    Elite       UMETA(DisplayName = "Elite"),
    Legendary   UMETA(DisplayName = "Legendary"),
};

UENUM(BlueprintType)
enum class ESharedQuestAcceptance : uint8
{
    IndividualAccept         UMETA(DisplayName = "Individual Accept"),
    LeaderAccept             UMETA(DisplayName = "Leader Accept"),
};

UENUM(BlueprintType)
enum class EQuestEventType : uint8
{
    BecameAvailable,
    BecameUnavailable,
    Started,
    StageAdvanced,
    Completed,
    Failed,
    Abandoned,
};

UENUM(BlueprintType)
enum class EQuestRejectionReason : uint8
{
    QuestNotActive,
    StageMismatch,
    ConditionFailed,
    AtCapacity,
    AlreadyActive,
    PermanentlyClosed,
    QuestDisabled,
};
