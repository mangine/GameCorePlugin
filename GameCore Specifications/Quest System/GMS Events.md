# GMS Events

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

All events are broadcast via `UGameCoreEventSubsystem` (the GMS wrapper). Channels follow the `GameCoreEvent.Quest.*` namespace. The quest system emits and never directly calls other systems — all downstream reactions (rewards, journal, achievements, UI, analytics) subscribe to these events.

---

## Event Channel Tags

Add to `DefaultGameplayTags.ini` in the Quest module:

```ini
; ── Quest lifecycle ─────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.Started",          DevComment="A player accepted and started a quest")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Completed",        DevComment="A quest was successfully completed")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Failed",           DevComment="A quest was failed")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Abandoned",        DevComment="A player abandoned an active quest")
+GameplayTagList=(Tag="GameCoreEvent.Quest.BecameAvailable",  DevComment="Unlock requirements passed; quest is now available to accept")

; ── Stage events ────────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageStarted",     DevComment="A quest stage became active")
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageCompleted",   DevComment="A quest stage was completed; quest advanced to next stage")
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageFailed",      DevComment="A quest stage failed (does not necessarily fail the quest)")

; ── Tracker events ───────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.TrackerUpdated",   DevComment="A progress tracker counter changed")

; ── Cadence resets (server-side, for journal and UI) ────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.DailyReset",       DevComment="Daily quest cadence reset occurred")
+GameplayTagList=(Tag="GameCoreEvent.Quest.WeeklyReset",      DevComment="Weekly quest cadence reset occurred")

; ── Party quest events ────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.PartyInvite",      DevComment="LeaderAccept: party members notified of pending quest acceptance")
+GameplayTagList=(Tag="GameCoreEvent.Quest.MemberLeft",       DevComment="A party member left a shared quest")
```

---

## Event Payload Structs

**File:** `Quest/Events/QuestEventPayloads.h`

```cpp
// ── Quest lifecycle payloads ─────────────────────────────────────────────────────

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
    // True if this failure permanently closes the quest (SingleAttempt).
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

// ── Stage payloads ──────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FQuestStageChangedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() FGameplayTag StageTag;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    // Localizable objective text from UQuestStageDefinition::StageObjectiveText.
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

// ── Party payloads ─────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FQuestPartyInvitePayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    // All PlayerStates being notified (excludes leader who already accepted).
    UPROPERTY() TArray<TWeakObjectPtr<APlayerState>> InvitedMembers;
    UPROPERTY() float GraceWindowSeconds = 10.0f;
};
```

---

## Emission Summary

| Event | Emitted By | When |
|---|---|---|
| `Quest.Started` | `UQuestComponent` | After enrollment completes |
| `Quest.Completed` | `UQuestComponent::Internal_CompleteQuest` | Terminal success state entered |
| `Quest.Failed` | `UQuestComponent::Internal_FailQuest` | Terminal failure state entered |
| `Quest.Abandoned` | `UQuestComponent::ServerRPC_AbandonQuest` | Player abandons |
| `Quest.BecameAvailable` | `UQuestComponent` (watcher callback) | Unlock requirements pass |
| `Quest.StageStarted` | `UQuestComponent::Internal_AdvanceStage` | New stage activated |
| `Quest.StageCompleted` | `UQuestComponent::Internal_AdvanceStage` | Previous stage completed |
| `Quest.StageFailed` | `UQuestComponent::Internal_FailQuest` (stage-level) | Stage failure state entered |
| `Quest.TrackerUpdated` | `UQuestComponent::Server_IncrementTracker` | Counter incremented |
| `Quest.DailyReset` | `UQuestRegistrySubsystem::OnDailyReset` | 00:00 UTC daily |
| `Quest.WeeklyReset` | `UQuestRegistrySubsystem::OnWeeklyReset` | Monday 00:00 UTC |
| `Quest.PartyInvite` | `UPartyQuestCoordinator::LeaderInitiateQuestAccept` | Leader accepts on behalf |
| `Quest.MemberLeft` | `UPartyQuestCoordinator::RemoveMember` | Member leaves party quest |

---

## RequirementEvent Tags (for Watcher System)

These are NOT `GameCoreEvent.*` — they are `RequirementEvent.*` tags used exclusively to invalidate cached requirement results in `URequirementWatcherComponent`.

| Tag | Fired When | Invalidates |
|---|---|---|
| `RequirementEvent.Quest.TrackerUpdated` | Any tracker counter changes | `URequirement_KillCount` and other persisted trackers |
| `RequirementEvent.Quest.StageChanged` | Stage advances | Any stage-dependent requirements |
| `RequirementEvent.Quest.Completed` | Quest completes | `URequirement_QuestCompleted`, `URequirement_QuestCooldown` |

These tags are fired by `UQuestComponent` directly via `URequirementWatcherManager::BroadcastEvent`, not via GMS. They are watcher-system internal signals.
