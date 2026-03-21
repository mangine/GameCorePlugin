# QuestEnums

**File:** `Quest/Enums/QuestEnums.h`

All quest-system enums. Included by every other Quest module file.

---

```cpp
#pragma once
#include "CoreMinimal.h"
#include "QuestEnums.generated.h"

// Quest repeat / permanence behaviour.
UENUM(BlueprintType)
enum class EQuestLifecycle : uint8
{
    // One attempt. On fail or complete: permanently closed.
    // QuestCompletedTag added on complete OR fail.
    SingleAttempt            UMETA(DisplayName = "Single Attempt"),

    // Repeatable until completed. On fail: reset to Available (cooldown applies).
    // On complete: permanently closed.
    RetryUntilComplete       UMETA(DisplayName = "Retry Until Complete"),

    // On complete: player can re-enter as Helper for other players.
    // On fail: reset to Available (cooldown applies).
    RetryAndAssist           UMETA(DisplayName = "Retry and Assist"),

    // Always repeatable. On complete or fail: reset to Available (cooldown applies).
    // Use for daily/weekly/event quests.
    Evergreen                UMETA(DisplayName = "Evergreen"),
};

// Who evaluates unlock and completion requirements.
UENUM(BlueprintType)
enum class EQuestCheckAuthority : uint8
{
    // Server is the sole evaluator for unlock and completion.
    // Unlock watcher runs server-only. Client is notified by RPC.
    // Use for: story gates, SingleAttempt, high-value rewards.
    ServerAuthoritative      UMETA(DisplayName = "Server Authoritative"),

    // Client also runs unlock and completion watchers for UI responsiveness.
    // Server always re-evaluates before acting. Client results are hints only.
    // Use for: common side quests, daily quests, high-volume quests.
    ClientValidated          UMETA(DisplayName = "Client Validated"),
};

// Reset schedule for Evergreen / RetryUntilComplete / RetryAndAssist quests.
// Expressed as URequirement_QuestCooldown in UnlockRequirements.
UENUM(BlueprintType)
enum class EQuestResetCadence : uint8
{
    None                     UMETA(DisplayName = "None (Fixed Cooldown)"),
    Daily                    UMETA(DisplayName = "Daily (00:00 UTC)"),
    Weekly                   UMETA(DisplayName = "Weekly (Monday 00:00 UTC)"),
    EventBound               UMETA(DisplayName = "Event Bound"),
};

// Member role within a shared quest.
// Only meaningful when USharedQuestComponent is in use.
UENUM(BlueprintType)
enum class EQuestMemberRole : uint8
{
    // Full participant — counts toward completion.
    Primary                  UMETA(DisplayName = "Primary"),

    // Helper re-run (RetryAndAssist lifecycle).
    // Receives helper reward table on completion.
    Helper                   UMETA(DisplayName = "Helper"),
};

// Visual difficulty tier. UI-facing only.
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

// How a group collectively accepts a shared quest.
// Only used by USharedQuestDefinition.
UENUM(BlueprintType)
enum class ESharedQuestAcceptance : uint8
{
    // Each member accepts independently via interaction.
    IndividualAccept         UMETA(DisplayName = "Individual Accept"),

    // Leader accepts and triggers enrollment for the group.
    // Members receive an invite with a grace window (USharedQuestDefinition::LeaderAcceptGraceSeconds).
    // The group system owns the invite/opt-out flow via OnRequestGroupEnrollment delegate.
    LeaderAccept             UMETA(DisplayName = "Leader Accept"),
};

// Events sent via ClientRPC_NotifyQuestEvent.
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

// Reasons a server-side validation can reject a client request.
UENUM(BlueprintType)
enum class EQuestRejectionReason : uint8
{
    QuestNotActive,
    StageMismatch,       // Client sent a stale stage tag
    ConditionFailed,     // Server re-evaluation failed
    AtCapacity,
    AlreadyActive,
    PermanentlyClosed,
    QuestDisabled,
};
```
