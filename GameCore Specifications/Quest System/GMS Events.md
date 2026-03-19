# GMS Events

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

All events are broadcast via `UGameCoreEventBus` (the GMS wrapper). Channels follow the `GameCoreEvent.Quest.*` namespace. The quest system emits events and never directly calls other systems — all downstream reactions (rewards, journal, achievements, UI, analytics) subscribe to these channels.

---

## Event Channel Tags

Add to `DefaultGameplayTags.ini` in the Quest module:

```ini
; ── Quest lifecycle ─────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.Started",         DevComment="Player accepted and started a quest")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Completed",       DevComment="Quest successfully completed")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Failed",          DevComment="Quest failed")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Abandoned",       DevComment="Player abandoned an active quest")
+GameplayTagList=(Tag="GameCoreEvent.Quest.BecameAvailable", DevComment="Unlock requirements passed; quest now available")

; ── Stage events ─────────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageStarted",    DevComment="A quest stage became active")
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageCompleted",  DevComment="Stage completed; quest advanced")
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageFailed",     DevComment="Stage failed (does not necessarily fail the quest)")

; ── Tracker events ───────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.TrackerUpdated",  DevComment="A progress tracker counter changed")

; ── Cadence resets ───────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.DailyReset",      DevComment="Daily quest cadence reset at 00:00 UTC")
+GameplayTagList=(Tag="GameCoreEvent.Quest.WeeklyReset",     DevComment="Weekly quest cadence reset at Monday 00:00 UTC")

; ── Shared quest events (only relevant when USharedQuestComponent is in use) ─────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.GroupInvite",     DevComment="LeaderAccept: group members notified of pending shared quest")
+GameplayTagList=(Tag="GameCoreEvent.Quest.MemberLeft",      DevComment="A member left a shared quest")
```

> **Note on `GroupInvite` and `MemberLeft`:** these two events are only ever emitted when `USharedQuestCoordinator` is active. Games not using the shared quest extension will never see them. They are defined here for completeness but are inert if `USharedQuestComponent` is not used.

---

## Event Payload Structs

**File:** `Quest/Events/QuestEventPayloads.h`

```cpp
// ── Quest lifecycle ────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FQuestStartedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    UPROPERTY() EQuestMemberRole MemberRole;
};

USTRUCT(BlueprintType)
struct FQuestCompletedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    UPROPERTY() EQuestMemberRole MemberRole;
    // Soft reference — reward system resolves and loads. Quest system never loads it.
    UPROPERTY() TSoftObjectPtr<ULootTable> RewardTable;
    // True if this is a Helper run (RetryAndAssist lifecycle).
    UPROPERTY() bool bIsHelperRun = false;
};

USTRUCT(BlueprintType)
struct FQuestFailedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    // True if this failure permanently closes the quest (SingleAttempt lifecycle).
    UPROPERTY() bool bPermanentlyClosed = false;
};

USTRUCT(BlueprintType)
struct FQuestAbandonedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
};

USTRUCT(BlueprintType)
struct FQuestBecameAvailablePayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
};

// ── Stage payloads ─────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FQuestStageChangedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() FGameplayTag StageTag;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    // From UQuestStageDefinition::StageObjectiveText — localizable.
    UPROPERTY() FText ObjectiveText;
};

// ── Tracker payloads ───────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FQuestTrackerUpdatedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() FGameplayTag TrackerKey;
    UPROPERTY() int32 OldValue = 0;
    UPROPERTY() int32 NewValue = 0;
    UPROPERTY() int32 EffectiveTarget = 1;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
};

// ── Shared quest payloads ──────────────────────────────────────────────────────────
// Only used when USharedQuestComponent / USharedQuestCoordinator are active.

USTRUCT(BlueprintType)
struct FQuestGroupInvitePayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    // Members pending enrollment (excludes leader who already accepted).
    UPROPERTY() TArray<TWeakObjectPtr<APlayerState>> InvitedMembers;
    UPROPERTY() float GraceWindowSeconds = 10.0f;
};

USTRUCT(BlueprintType)
struct FQuestMemberLeftPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> LeavingMember;
    UPROPERTY() int32 RemainingMemberCount = 0;
};
```

---

## Emission Summary

| Event | Emitted By | When |
|---|---|---|
| `Quest.Started` | `UQuestComponent` | Enrollment complete |
| `Quest.Completed` | `UQuestComponent::Internal_CompleteQuest` | Terminal success state entered |
| `Quest.Failed` | `UQuestComponent::Internal_FailQuest` | Terminal failure state entered |
| `Quest.Abandoned` | `UQuestComponent::ServerRPC_AbandonQuest` | Player abandons |
| `Quest.BecameAvailable` | `UQuestComponent` watcher callback | Unlock requirements pass |
| `Quest.StageStarted` | `UQuestComponent::Internal_AdvanceStage` | New stage activated |
| `Quest.StageCompleted` | `UQuestComponent::Internal_AdvanceStage` | Previous stage completed |
| `Quest.StageFailed` | `UQuestComponent::Internal_FailQuest` | Stage failure state entered |
| `Quest.TrackerUpdated` | `UQuestComponent::Server_IncrementTracker` | Counter incremented (called externally) |
| `Quest.DailyReset` | `UQuestRegistrySubsystem::OnDailyReset` | 00:00 UTC daily |
| `Quest.WeeklyReset` | `UQuestRegistrySubsystem::OnWeeklyReset` | Monday 00:00 UTC |
| `Quest.GroupInvite` | `USharedQuestCoordinator::LeaderInitiateAccept` | Leader accepts on behalf of group |
| `Quest.MemberLeft` | `USharedQuestCoordinator::RemoveMember` | Member leaves shared quest |

---

## RequirementEvent Tags (Watcher System Internal)

These are **not** `GameCoreEvent.*` tags. They live under `RequirementEvent.*` and are used exclusively to invalidate cached requirement results inside `URequirementWatcherComponent`. They are fired by `UQuestComponent` via `URequirementWatcherManager::NotifyPlayerEvent` — not via GMS.

| Tag | Fired By | Invalidates |
|---|---|---|
| `RequirementEvent.Quest.TrackerUpdated` | `UQuestComponent::Server_IncrementTracker` | Tracker-based requirements (`URequirement_Persisted` subclasses) |
| `RequirementEvent.Quest.StageChanged` | `UQuestComponent::Internal_AdvanceStage` | Any stage-gated requirements |
| `RequirementEvent.Quest.Completed` | `UQuestComponent::Internal_CompleteQuest` | `URequirement_QuestCompleted`, `URequirement_QuestCooldown` |
