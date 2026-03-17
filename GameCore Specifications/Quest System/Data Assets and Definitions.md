# Data Assets and Definitions

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

All types on this page are pure data — no mutable runtime state. They are loaded by `UQuestRegistrySubsystem` on demand and shared across all players.

---

## Enums

**File:** `Quest/Enums/QuestEnums.h`

```cpp
// How many times and under what conditions a quest can be attempted.
UENUM(BlueprintType)
enum class EQuestLifecycle : uint8
{
    // One attempt only. Fail or complete = closed forever.
    // Player cannot join a party to retry as a helper.
    SingleAttempt            UMETA(DisplayName = "Single Attempt"),

    // Fail resets the quest to Available. Retry unlimited.
    // Party participation allowed at any time.
    RetryUntilComplete       UMETA(DisplayName = "Retry Until Complete"),

    // As RetryUntilComplete, PLUS: player can rejoin as Helper
    // even after completing the quest (Helper rewards from RepeatingRewardTable).
    RetryAndAssist           UMETA(DisplayName = "Retry and Assist"),

    // Always available after each completion/failure.
    // Cooldown via URequirement_QuestCooldown if desired.
    Evergreen                UMETA(DisplayName = "Evergreen"),
};

// Whether the quest must be validated on server first or client first.
UENUM(BlueprintType)
enum class EQuestCheckAuthority : uint8
{
    // Server evaluates requirements. Notifies client via RPC on pass.
    // Higher security, higher server load.
    ServerAuthoritative      UMETA(DisplayName = "Server Authoritative"),

    // Client evaluates for UI responsiveness. Fires validation RPC to server.
    // Server re-evaluates fully — never trusts client result.
    ClientValidated          UMETA(DisplayName = "Client Validated"),
};

// Periodic reset cadence for Evergreen / RetryUntilComplete quests.
UENUM(BlueprintType)
enum class EQuestResetCadence : uint8
{
    // No calendar-based reset. URequirement_QuestCooldown uses elapsed time.
    None                     UMETA(DisplayName = "None"),

    // Resets daily at 00:00 UTC.
    Daily                    UMETA(DisplayName = "Daily"),

    // Resets weekly at Monday 00:00 UTC.
    Weekly                   UMETA(DisplayName = "Weekly"),

    // Tied to a server event window. ExpiryTimeSeconds governs availability.
    // Quest becomes unavailable when the event ends, regardless of completion.
    EventBound               UMETA(DisplayName = "Event Bound"),
};

// Group size requirement for accepting this quest.
UENUM(BlueprintType)
enum class EGroupRequirement : uint8
{
    // Solo or group. No restriction.
    None                     UMETA(DisplayName = "None"),

    // Party supported but not mandatory. Solo accepted.
    GroupOptional            UMETA(DisplayName = "Group Optional"),

    // Cannot accept without an active party of at least MinGroupSize.
    GroupRequired            UMETA(DisplayName = "Group Required"),

    // Must be in a party AND party quest coordinator must be active.
    // Solo accept is blocked even if party is size 1.
    GroupOnly                UMETA(DisplayName = "Group Only"),
};

// How a party accepts a quest together.
UENUM(BlueprintType)
enum class EPartyQuestAcceptance : uint8
{
    // Each member accepts individually via their own quest UI.
    // Shared tracker starts once all enrolled members have accepted.
    IndividualAccept         UMETA(DisplayName = "Individual Accept"),

    // Party leader accepts on behalf of all members.
    // Opt-out grace window (LeaderAcceptGraceSeconds) before trackers start.
    // Members who do not opt out are enrolled automatically.
    LeaderAccept             UMETA(DisplayName = "Leader Accept"),
};

// Role of this player in a party quest.
UENUM(BlueprintType)
enum class EQuestMemberRole : uint8
{
    // Primary attempt — rewards from FirstTimeRewardTable.
    Primary                  UMETA(DisplayName = "Primary"),

    // Helping another player — rewards from RepeatingRewardTable.
    // Only valid on RetryAndAssist quests.
    Helper                   UMETA(DisplayName = "Helper"),
};

// Difficulty classification shown in quest UI.
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
```

---

## `FQuestDisplayData`

**File:** `Quest/Data/QuestDisplayData.h`

```cpp
// All UI-facing metadata for a quest. FText fields are localizable.
// QuestImage is a soft reference — loaded on demand by UI, never during gameplay.
USTRUCT(BlueprintType)
struct PIRATEQUESTS_API FQuestDisplayData
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    FText Title;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    FText ShortDescription;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    FText LongDescription;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    EQuestDifficulty Difficulty = EQuestDifficulty::Normal;

    // Soft reference — not loaded at definition load time.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    TSoftObjectPtr<UTexture2D> QuestImage;
};
```

---

## `FQuestProgressTrackerDef`

**File:** `Quest/Data/QuestStageDefinition.h`

```cpp
// Defines one progress counter for a quest stage.
// At runtime, this maps to a FQuestTrackerEntry with the same TrackerKey.
USTRUCT(BlueprintType)
struct PIRATEQUESTS_API FQuestProgressTrackerDef
{
    GENERATED_BODY()

    // Unique key for this tracker within the stage.
    // Used as the counter key inside FRequirementPayload.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
              meta=(Categories="Quest.Counter"))
    FGameplayTag TrackerKey;

    // Baseline target value for a solo player.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin=1))
    int32 TargetValue = 1;

    // Per-additional-party-member multiplier.
    // EffectiveTarget = TargetValue + (PartySize - 1) * TargetValue * ScalingMultiplier
    // ScalingMultiplier = 0.0 means this tracker does not scale with party size.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
              meta=(ClampMin=0.0f, ClampMax=1.0f))
    float ScalingMultiplier = 0.0f;

    // If true, the system does not maintain a persistent counter for this tracker.
    // CompletionRequirements re-evaluate this condition on every check.
    // Use for inventory checks, tag checks, or any condition that is always
    // derivable from current world state without needing a running tally.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    bool bReEvaluateOnly = false;

    // Compute effective target for a given party size.
    int32 GetEffectiveTarget(int32 PartySize) const
    {
        if (PartySize <= 1 || ScalingMultiplier <= 0.0f) return TargetValue;
        return FMath::RoundToInt(
            TargetValue + (PartySize - 1) * TargetValue * ScalingMultiplier);
    }
};
```

---

## `UQuestStageDefinition`

**File:** `Quest/Data/QuestStageDefinition.h / .cpp`

```cpp
// Defines the data and requirements for one stage in a quest's state machine.
// Instanced inside UQuestDefinition::Stages — one entry per StateTag in the
// UStateMachineAsset. The StateTag must match a state node in the asset.
UCLASS(EditInlineNew, CollapseCategories, BlueprintType)
class PIRATEQUESTS_API UQuestStageDefinition : public UObject
{
    GENERATED_BODY()
public:
    // Must match a FGameplayTag state node in the owning UQuestDefinition::StageGraph.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FGameplayTag StageTag;

    // Requirements that must pass for this stage to be considered complete.
    // Authority determined by UQuestDefinition::CheckAuthority.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    TObjectPtr<URequirementList> CompletionRequirements;

    // Progress trackers active during this stage.
    // Each entry with bReEvaluateOnly=false creates a live FQuestTrackerEntry
    // in the player's FQuestRuntime.
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="Stage")
    TArray<FQuestProgressTrackerDef> Trackers;

    // Optional display text shown to the player when this stage becomes active.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FText StageObjectiveText;

    // Is this a terminal failure state? If true, the quest system triggers
    // the failure flow when the state machine enters this state.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    bool bIsFailureState = false;

    // Is this a terminal success state? If true, the quest system triggers
    // the completion flow when the state machine enters this state.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    bool bIsCompletionState = false;

#if WITH_EDITOR
    EDataValidationResult IsDataValid(FDataValidationContext& Context) const;
#endif
};
```

> **Note:** Both `bIsFailureState` and `bIsCompletionState` can be false — intermediate stages with no terminal effect. The state machine handles branching; these flags are read by `UQuestComponent` to trigger lifecycle changes when the machine enters the state.

---

## `UQuestDefinition`

**File:** `Quest/Data/QuestDefinition.h / .cpp`

```cpp
UCLASS(BlueprintType)
class PIRATEQUESTS_API UQuestDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // ── Identity ─────────────────────────────────────────────────────────────

    // Unique tag identifying this quest. Used as the key in FQuestRuntime,
    // CompletedQuestTags, and all GMS events.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Id"))
    FGameplayTag QuestId;

    // Tag added to UQuestComponent::CompletedQuestTags on completion OR
    // permanent failure. Used as a fast pre-filter before loading the definition.
    // Must be unique per quest. Convention: Quest.Completed.<QuestName>
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Completed"))
    FGameplayTag QuestCompletedTag;

    // ── Lifecycle & Rules ─────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestLifecycle Lifecycle = EQuestLifecycle::RetryUntilComplete;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestCheckAuthority CheckAuthority = EQuestCheckAuthority::ClientValidated;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestResetCadence ResetCadence = EQuestResetCadence::None;

    // Only meaningful when ResetCadence == EventBound.
    // Unix timestamp (seconds). 0 = no expiry.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    int64 ExpiryTimestamp = 0;

    // ── Stage Graph ───────────────────────────────────────────────────────────

    // The state machine asset driving stage transitions.
    // EntryStateTag in this asset is the first active stage.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Stages")
    TObjectPtr<UStateMachineAsset> StageGraph;

    // One entry per state node in StageGraph.
    // Instanced — each UQuestStageDefinition is owned by this asset.
    // Keyed by UQuestStageDefinition::StageTag.
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="Quest|Stages")
    TArray<TObjectPtr<UQuestStageDefinition>> Stages;

    // ── Unlock Requirements ───────────────────────────────────────────────────

    // Requirements that must pass for this quest to become Available.
    // Authority is CheckAuthority. Watched by URequirementWatcherComponent.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Requirements")
    TObjectPtr<URequirementList> UnlockRequirements;

    // ── Group Settings ────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group")
    EGroupRequirement GroupRequirement = EGroupRequirement::None;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group",
              meta=(EditCondition="GroupRequirement != EGroupRequirement::None",
                    ClampMin=2, ClampMax=40))
    int32 MinGroupSize = 2;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group",
              meta=(EditCondition="GroupRequirement != EGroupRequirement::None",
                    ClampMin=2, ClampMax=40))
    int32 MaxGroupSize = 5;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group",
              meta=(EditCondition="GroupRequirement != EGroupRequirement::None"))
    EPartyQuestAcceptance PartyAcceptance = EPartyQuestAcceptance::IndividualAccept;

    // Seconds after leader accepts before trackers start and opt-out window closes.
    // Only used when PartyAcceptance == LeaderAccept.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group",
              meta=(EditCondition="PartyAcceptance == EPartyQuestAcceptance::LeaderAccept",
                    ClampMin=0.0f))
    float LeaderAcceptGraceSeconds = 10.0f;

    // Derived — true if GroupRequirement is not None.
    bool SupportsParty() const
    {
        return GroupRequirement != EGroupRequirement::None;
    }

    // ── Rewards ───────────────────────────────────────────────────────────────

    // Soft references — loaded by the reward system on completion, not by the
    // quest system itself. Quest system passes these tags in the Completed GMS event.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> FirstTimeRewardTable;

    // Used for Evergreen repeats, RetryAndAssist helper runs.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> RepeatingRewardTable;

    // ── Categorisation & Display ──────────────────────────────────────────────

    // Mapped to icon in UQuestMarkerDataAsset. Examples:
    // Quest.Marker.MainStory, Quest.Marker.SideQuest, Quest.Marker.Daily
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FGameplayTag QuestMarkerTag;

    // Freeform categorisation for UI filtering and grouping.
    // Examples: Quest.Category.Combat, Quest.Category.Exploration
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FGameplayTagContainer QuestCategories;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FQuestDisplayData Display;

    // ── API ───────────────────────────────────────────────────────────────────

    // Returns the stage definition for a given state tag, or nullptr.
    const UQuestStageDefinition* FindStage(const FGameplayTag& StageTag) const;

    // Returns the effective tracker target for a given tracker key and party size.
    int32 GetEffectiveTrackerTarget(
        const FGameplayTag& TrackerKey, int32 PartySize) const;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
    // Validates: QuestId set, QuestCompletedTag set, StageGraph set,
    // all Stages have matching StateTag in StageGraph, at least one
    // bIsCompletionState stage exists.
#endif

    // UPrimaryDataAsset
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("QuestDefinition"), GetFName());
    }
};
```

---

## `UQuestMarkerDataAsset`

**File:** `Quest/Data/QuestMarkerDataAsset.h / .cpp`

```cpp
// Maps quest marker tags to UI icons. One asset per project.
// Referenced by the quest UI system — not by the quest system itself.
UCLASS(BlueprintType)
class PIRATEQUESTS_API UQuestMarkerDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Markers")
    TMap<FGameplayTag, TSoftObjectPtr<UTexture2D>> MarkerIcons;

    // Returns the icon for a marker tag, or nullptr if not mapped.
    TSoftObjectPtr<UTexture2D> GetIcon(const FGameplayTag& MarkerTag) const
    {
        const TSoftObjectPtr<UTexture2D>* Found = MarkerIcons.Find(MarkerTag);
        return Found ? *Found : TSoftObjectPtr<UTexture2D>();
    }
};
```
