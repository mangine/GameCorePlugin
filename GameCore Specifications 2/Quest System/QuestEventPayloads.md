# Quest Event Payloads

**File:** `Quest/Events/QuestEventPayloads.h`

All GMS event channel tags and payload structs emitted by the quest system. The quest system emits; it never subscribes.

---

## Gameplay Tag Definitions (`DefaultGameplayTags.ini`)

```ini
; ── Quest lifecycle ──────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.Started",          DevComment="Player accepted and started a quest")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Completed",        DevComment="Quest successfully completed")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Failed",           DevComment="Quest failed")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Abandoned",        DevComment="Player abandoned an active quest")
+GameplayTagList=(Tag="GameCoreEvent.Quest.BecameAvailable",  DevComment="Unlock requirements passed; quest now available")

; ── Stage events ─────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageStarted",     DevComment="A quest stage became active")
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageCompleted",   DevComment="Stage completed; quest advanced to next stage")
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageFailed",      DevComment="Stage failed (does not necessarily fail the quest)")

; ── Tracker events ───────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.TrackerUpdated",   DevComment="A progress tracker counter changed")

; ── Cadence resets ───────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.DailyReset",       DevComment="Daily quest cadence reset at 00:00 UTC")
+GameplayTagList=(Tag="GameCoreEvent.Quest.WeeklyReset",      DevComment="Weekly quest cadence reset at Monday 00:00 UTC")

; ── Shared quest events ──────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.GroupInvite",      DevComment="LeaderAccept: group members notified of pending shared quest")
+GameplayTagList=(Tag="GameCoreEvent.Quest.MemberLeft",       DevComment="A member left or disconnected from a shared quest")

; ── Identity / counter namespaces ────────────────────────────────────────────────
+GameplayTagList=(Tag="Quest.Id")
+GameplayTagList=(Tag="Quest.Completed")
+GameplayTagList=(Tag="Quest.Counter")

; ── Categories ───────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="Quest.Category.Story")
+GameplayTagList=(Tag="Quest.Category.SideQuest")
+GameplayTagList=(Tag="Quest.Category.Daily")
+GameplayTagList=(Tag="Quest.Category.Weekly")
+GameplayTagList=(Tag="Quest.Category.Dungeon")
+GameplayTagList=(Tag="Quest.Category.Event")
+GameplayTagList=(Tag="Quest.Category.NPC")

; ── Markers ──────────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="Quest.Marker.MainStory")
+GameplayTagList=(Tag="Quest.Marker.SideQuest")
+GameplayTagList=(Tag="Quest.Marker.Daily")
+GameplayTagList=(Tag="Quest.Marker.Dungeon")
+GameplayTagList=(Tag="Quest.Marker.Event")
+GameplayTagList=(Tag="Quest.Marker.NPC")

; ── Requirement invalidation events ─────────────────────────────────────────────
+GameplayTagList=(Tag="RequirementEvent.Quest.TrackerUpdated")
+GameplayTagList=(Tag="RequirementEvent.Quest.StageChanged")
+GameplayTagList=(Tag="RequirementEvent.Quest.Completed")
```

---

## Payload Structs

```cpp
// ── Quest lifecycle ─────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FQuestStartedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    UPROPERTY() EQuestMemberRole MemberRole = EQuestMemberRole::Primary;
};

USTRUCT(BlueprintType)
struct FQuestCompletedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    UPROPERTY() EQuestMemberRole MemberRole = EQuestMemberRole::Primary;
    // Soft reference — reward system loads and grants. Quest system never loads it.
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

// ── Stage payloads ──────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FQuestStageChangedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() FGameplayTag StageTag;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    // Localised objective text from UQuestStageDefinition::StageObjectiveText.
    UPROPERTY() FText ObjectiveText;
};

// ── Tracker payload ─────────────────────────────────────────────────────────────

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

// ── Cadence reset payload ───────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FQuestResetPayload
{
    GENERATED_BODY()
    UPROPERTY() EQuestResetCadence Cadence = EQuestResetCadence::Daily;
};

// ── Shared quest payloads ───────────────────────────────────────────────────────
// Only emitted when USharedQuestComponent / USharedQuestCoordinator are active.

USTRUCT(BlueprintType)
struct FQuestGroupInvitePayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
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

| Event Tag | Emitted By | When |
|---|---|---|
| `Quest.Started` | `UQuestComponent` | Quest accepted and runtime created |
| `Quest.Completed` | `Internal_CompleteQuest` | Terminal success state entered |
| `Quest.Failed` | `Internal_FailQuest` | Terminal failure state entered |
| `Quest.Abandoned` | `ServerRPC_AbandonQuest` | Player abandons |
| `Quest.BecameAvailable` | Watcher callback (client RPC) | Unlock requirements pass |
| `Quest.StageStarted` | `Internal_AdvanceStage` | New stage activated |
| `Quest.StageCompleted` | `Internal_AdvanceStage` | Previous stage completed |
| `Quest.StageFailed` | `Internal_FailQuest` | Stage failure state entered |
| `Quest.TrackerUpdated` | `Server_IncrementTracker` | Counter incremented |
| `Quest.DailyReset` | `UQuestRegistrySubsystem::OnDailyReset` | 00:00 UTC |
| `Quest.WeeklyReset` | `UQuestRegistrySubsystem::OnWeeklyReset` | Monday 00:00 UTC |
| `Quest.GroupInvite` | `USharedQuestCoordinator::LeaderInitiateAccept` | Leader accepts on behalf of group |
| `Quest.MemberLeft` | `USharedQuestCoordinator::RemoveMember` | Member leaves shared quest |

---

## RequirementEvent Tags (Watcher Invalidation)

These live under `RequirementEvent.*` and are used exclusively to invalidate cached results inside the watcher system. Fired by `UQuestComponent` — **not** via GMS.

| Tag | Fired By | Invalidates |
|---|---|---|
| `RequirementEvent.Quest.TrackerUpdated` | `Server_IncrementTracker` | Tracker-based requirements |
| `RequirementEvent.Quest.StageChanged` | `Internal_AdvanceStage` | Stage-gated requirements |
| `RequirementEvent.Quest.Completed` | `Internal_CompleteQuest` | `URequirement_QuestCompleted`, `URequirement_QuestCooldown` |
