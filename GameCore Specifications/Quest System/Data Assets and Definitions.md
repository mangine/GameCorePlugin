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
    SingleAttempt            UMETA(DisplayName = "Single Attempt"),
    RetryUntilComplete       UMETA(DisplayName = "Retry Until Complete"),
    RetryAndAssist           UMETA(DisplayName = "Retry and Assist"),
    Evergreen                UMETA(DisplayName = "Evergreen"),
};

// Whether the quest must be validated on server first or client first.
UENUM(BlueprintType)
enum class EQuestCheckAuthority : uint8
{
    ServerAuthoritative      UMETA(DisplayName = "Server Authoritative"),
    ClientValidated          UMETA(DisplayName = "Client Validated"),
};

// Periodic reset cadence for repeatable quests.
UENUM(BlueprintType)
enum class EQuestResetCadence : uint8
{
    None                     UMETA(DisplayName = "None"),
    Daily                    UMETA(DisplayName = "Daily"),       // 00:00 UTC
    Weekly                   UMETA(DisplayName = "Weekly"),      // Monday 00:00 UTC
    EventBound               UMETA(DisplayName = "Event Bound"), // Governed by ExpiryTimestamp
};

// Role of this player in a shared quest run.
UENUM(BlueprintType)
enum class EQuestMemberRole : uint8
{
    Primary                  UMETA(DisplayName = "Primary"),
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

// Group size constraint for shared quests. Defined here for use by USharedQuestDefinition.
// Has no effect on UQuestDefinition — the base quest system has no group concept.
UENUM(BlueprintType)
enum class EGroupRequirement : uint8
{
    None                     UMETA(DisplayName = "None"),
    GroupOptional            UMETA(DisplayName = "Group Optional"),
    GroupRequired            UMETA(DisplayName = "Group Required"),
    GroupOnly                UMETA(DisplayName = "Group Only"),
};

// How a group collectively accepts a shared quest.
UENUM(BlueprintType)
enum class ESharedQuestAcceptance : uint8
{
    IndividualAccept         UMETA(DisplayName = "Individual Accept"),
    LeaderAccept             UMETA(DisplayName = "Leader Accept"),
};
```

---

## `FQuestDisplayData`

**File:** `Quest/Data/QuestDisplayData.h`

```cpp
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

    // Soft reference — loaded by UI on demand, never by the quest system.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Display")
    TSoftObjectPtr<UTexture2D> QuestImage;
};
```

---

## `FQuestProgressTrackerDef`

**File:** `Quest/Data/QuestStageDefinition.h`

```cpp
USTRUCT(BlueprintType)
struct PIRATEQUESTS_API FQuestProgressTrackerDef
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
              meta=(Categories="Quest.Counter"))
    FGameplayTag TrackerKey;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin=1))
    int32 TargetValue = 1;

    // Per-additional-member multiplier. 0 = non-scalable (direct copy on de-scale).
    // EffectiveTarget = TargetValue + (GroupSize - 1) * TargetValue * ScalingMultiplier
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
              meta=(ClampMin=0.0f, ClampMax=1.0f))
    float ScalingMultiplier = 0.0f;

    // If true, no persistent counter is maintained. CompletionRequirements
    // re-evaluate this condition on every check from live world state.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    bool bReEvaluateOnly = false;

    int32 GetEffectiveTarget(int32 GroupSize) const
    {
        if (GroupSize <= 1 || ScalingMultiplier <= 0.0f) return TargetValue;
        return FMath::RoundToInt(
            TargetValue + (GroupSize - 1) * TargetValue * ScalingMultiplier);
    }
};
```

---

## `UQuestStageDefinition`

**File:** `Quest/Data/QuestStageDefinition.h / .cpp`

```cpp
UCLASS(EditInlineNew, CollapseCategories, BlueprintType)
class PIRATEQUESTS_API UQuestStageDefinition : public UObject
{
    GENERATED_BODY()
public:
    // Must match a state tag in UQuestDefinition::StageGraph.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FGameplayTag StageTag;

    // Requirements evaluated to determine if this stage is complete.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    TObjectPtr<URequirementList> CompletionRequirements;

    // Trackers active while this stage is the current stage.
    // Entries with bReEvaluateOnly=false produce a live FQuestTrackerEntry
    // in FQuestRuntime when this stage becomes active.
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="Stage")
    TArray<FQuestProgressTrackerDef> Trackers;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FText StageObjectiveText;

    // Entering this state triggers the quest failure flow in UQuestComponent.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    bool bIsFailureState = false;

    // Entering this state triggers the quest completion flow in UQuestComponent.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    bool bIsCompletionState = false;

#if WITH_EDITOR
    EDataValidationResult IsDataValid(FDataValidationContext& Context) const;
#endif
};
```

---

## `UQuestDefinition`

**File:** `Quest/Data/QuestDefinition.h / .cpp`

The base class. Contains everything needed for a solo quest system. Has no group or sharing concepts.

```cpp
UCLASS(BlueprintType)
class PIRATEQUESTS_API UQuestDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // ── Identity ────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Id"))
    FGameplayTag QuestId;

    // Added to CompletedQuestTags on permanent close (complete or SingleAttempt fail).
    // Used as O(1) pre-filter before loading the definition.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Completed"))
    FGameplayTag QuestCompletedTag;

    // ── Live-ops ────────────────────────────────────────────────────────────

    // Kill switch for bugged or temporarily disabled quests.
    // Disabled quests are excluded from candidate unlock lists.
    // Active quests with bEnabled=false are removed from ActiveQuests on login
    // WITHOUT adding QuestCompletedTag — so re-enabling the quest makes it
    // available again immediately.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    bool bEnabled = true;

    // ── Lifecycle & Rules ───────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestLifecycle Lifecycle = EQuestLifecycle::RetryUntilComplete;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestCheckAuthority CheckAuthority = EQuestCheckAuthority::ClientValidated;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestResetCadence ResetCadence = EQuestResetCadence::None;

    // Unix timestamp. 0 = no expiry. Meaningful only for EventBound cadence.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    int64 ExpiryTimestamp = 0;

    // ── Stage Graph ───────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Stages")
    TObjectPtr<UStateMachineAsset> StageGraph;

    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="Quest|Stages")
    TArray<TObjectPtr<UQuestStageDefinition>> Stages;

    // ── Requirements ───────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Requirements")
    TObjectPtr<URequirementList> UnlockRequirements;

    // ── Rewards ───────────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> FirstTimeRewardTable;

    // Used for Evergreen repeats and RetryAndAssist helper runs.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> RepeatingRewardTable;

    // ── Categorisation & Display ──────────────────────────────────────────────

    // Mapped to icon via UQuestMarkerDataAsset. e.g. Quest.Marker.MainStory
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FGameplayTag QuestMarkerTag;

    // Freeform UI filtering. e.g. Quest.Category.Story, Quest.Category.Combat
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FGameplayTagContainer QuestCategories;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FQuestDisplayData Display;

    // ── API ───────────────────────────────────────────────────────────────────

    const UQuestStageDefinition* FindStage(const FGameplayTag& StageTag) const;

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("QuestDefinition"), GetFName());
    }

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
    // Validates: QuestId set, QuestCompletedTag set, StageGraph set,
    // all Stages have a matching StateTag in StageGraph,
    // at least one bIsCompletionState stage exists.
#endif
};
```

---

## `USharedQuestDefinition`

**File:** `Quest/Data/SharedQuestDefinition.h / .cpp`

Extends `UQuestDefinition` with group/sharing configuration. The base quest system has no knowledge of this class. `USharedQuestComponent` upcasts to it when present.

```cpp
UCLASS(BlueprintType)
class PIRATEQUESTS_API USharedQuestDefinition : public UQuestDefinition
{
    GENERATED_BODY()
public:

    // ── Group Constraints ────────────────────────────────────────────────────

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
    ESharedQuestAcceptance AcceptanceMode = ESharedQuestAcceptance::IndividualAccept;

    // Opt-out grace window after leader accepts on behalf of the group.
    // Only meaningful when AcceptanceMode == LeaderAccept.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group",
              meta=(EditCondition="AcceptanceMode == ESharedQuestAcceptance::LeaderAccept",
                    ClampMin=0.0f))
    float LeaderAcceptGraceSeconds = 10.0f;

    // True if this quest supports passive tracker contribution from group members
    // who have the quest active independently (not formally shared).
    // When true, USharedQuestComponent fans out tracker increments from group
    // member kill/interaction events to this player's active quest trackers.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group")
    bool bAllowPassiveGroupContribution = true;

    // ── API ───────────────────────────────────────────────────────────────────

    bool SupportsGroup() const
    {
        return GroupRequirement != EGroupRequirement::None;
    }

    // True if the player satisfies the group size constraint via IGroupProvider.
    // Returns true unconditionally if GroupRequirement == None.
    bool IsGroupSizeValid(int32 CurrentGroupSize) const
    {
        if (GroupRequirement == EGroupRequirement::None) return true;
        if (GroupRequirement == EGroupRequirement::GroupOnly ||
            GroupRequirement == EGroupRequirement::GroupRequired)
        {
            return CurrentGroupSize >= MinGroupSize &&
                   CurrentGroupSize <= MaxGroupSize;
        }
        // GroupOptional: any size is valid
        return true;
    }

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("QuestDefinition"), GetFName());
        // Same asset type as base — loaded by the same registry.
    }
};
```

> **Design note:** Both `UQuestDefinition` and `USharedQuestDefinition` register under the `"QuestDefinition"` primary asset type. `UQuestRegistrySubsystem` loads all of them identically. `USharedQuestComponent::ServerRPC_AcceptQuest` upcasts the loaded definition to `USharedQuestDefinition*` — if the cast fails, it falls back to solo behavior. This means a `USharedQuestComponent` can accept and run plain `UQuestDefinition` assets without issue.

---

## `UQuestMarkerDataAsset`

**File:** `Quest/Data/QuestMarkerDataAsset.h / .cpp`

```cpp
UCLASS(BlueprintType)
class PIRATEQUESTS_API UQuestMarkerDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Markers")
    TMap<FGameplayTag, TSoftObjectPtr<UTexture2D>> MarkerIcons;

    TSoftObjectPtr<UTexture2D> GetIcon(const FGameplayTag& MarkerTag) const
    {
        const TSoftObjectPtr<UTexture2D>* Found = MarkerIcons.Find(MarkerTag);
        return Found ? *Found : TSoftObjectPtr<UTexture2D>();
    }
};
```
