# Data Assets and Definitions

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

All types on this page are pure data — no mutable runtime state. They are loaded by `UQuestRegistrySubsystem` on demand and shared across all players.

---

## Enums

**File:** `Quest/Enums/QuestEnums.h`

```cpp
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
    None                     UMETA(DisplayName = "None"),
    Daily                    UMETA(DisplayName = "Daily"),
    Weekly                   UMETA(DisplayName = "Weekly"),
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

// How a group collectively accepts a shared quest.
// Only used by USharedQuestDefinition.
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
struct GAMECORE_API FQuestDisplayData
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
struct GAMECORE_API FQuestProgressTrackerDef
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
              meta=(Categories="Quest.Counter"))
    FGameplayTag TrackerKey;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin=1))
    int32 TargetValue = 1;

    // Per-additional-member multiplier for shared quests.
    // 0 = non-scalable; shared members contribute but target stays fixed.
    // EffectiveTarget = TargetValue + (GroupSize - 1) * TargetValue * ScalingMultiplier
    // When GroupSize == 1 (solo), GetEffectiveTarget always returns TargetValue.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
              meta=(ClampMin=0.0f, ClampMax=1.0f))
    float ScalingMultiplier = 0.0f;

    // If true, no persistent counter is maintained.
    // CompletionRequirements re-evaluate from live world state on every check.
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
class GAMECORE_API UQuestStageDefinition : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FGameplayTag StageTag;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    TObjectPtr<URequirementList> CompletionRequirements;

    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="Stage")
    TArray<FQuestProgressTrackerDef> Trackers;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FText StageObjectiveText;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    bool bIsFailureState = false;

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

Base class. Contains everything needed for a solo quest. No group or sharing concepts.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UQuestDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // ── Identity ────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Id"))
    FGameplayTag QuestId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Completed"))
    FGameplayTag QuestCompletedTag;

    // ── Live-ops ───────────────────────────────────────────────────────────

    // Kill switch. Disabled quests are excluded from candidate unlock lists.
    // Active disabled quests are removed on login WITHOUT adding QuestCompletedTag
    // so re-enabling makes them immediately available again.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    bool bEnabled = true;

    // ── Lifecycle & Rules ──────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestLifecycle Lifecycle = EQuestLifecycle::RetryUntilComplete;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestCheckAuthority CheckAuthority = EQuestCheckAuthority::ClientValidated;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestResetCadence ResetCadence = EQuestResetCadence::None;

    // Unix timestamp. 0 = no expiry. Meaningful only for EventBound cadence.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    int64 ExpiryTimestamp = 0;

    // ── Stage Graph ─────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Stages")
    TObjectPtr<UStateMachineAsset> StageGraph;

    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="Quest|Stages")
    TArray<TObjectPtr<UQuestStageDefinition>> Stages;

    // ── Requirements ────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Requirements")
    TObjectPtr<URequirementList> UnlockRequirements;

    // ── Rewards ──────────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> FirstTimeRewardTable;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> RepeatingRewardTable;

    // ── Categorisation & Display ──────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FGameplayTag QuestMarkerTag;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FGameplayTagContainer QuestCategories;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FQuestDisplayData Display;

    // ── API ──────────────────────────────────────────────────────────────────

    const UQuestStageDefinition* FindStage(const FGameplayTag& StageTag) const;

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("QuestDefinition"), GetFName());
    }

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
#endif
};
```

---

## `USharedQuestDefinition`

**File:** `Quest/Data/SharedQuestDefinition.h / .cpp`

Extends `UQuestDefinition` with group enrollment configuration. The base quest system has no knowledge of this class. `USharedQuestComponent` upcasts to it when present. When played solo (no `IGroupProvider` bound), behaves identically to `UQuestDefinition` — `ScalingMultiplier` returns `TargetValue` unchanged for `GroupSize == 1`.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API USharedQuestDefinition : public UQuestDefinition
{
    GENERATED_BODY()
public:

    // ── Group Enrollment ───────────────────────────────────────────────────

    // How the group collectively accepts this quest.
    // IndividualAccept: each member accepts independently.
    // LeaderAccept: leader triggers enrollment for all; members have a grace
    //               window to opt out via the external group system.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group")
    ESharedQuestAcceptance AcceptanceMode = ESharedQuestAcceptance::IndividualAccept;

    // Grace window duration passed to OnRequestGroupEnrollment on the coordinator.
    // Only meaningful when AcceptanceMode == LeaderAccept.
    // The external group system owns the timer — this value is metadata only.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group",
              meta=(EditCondition="AcceptanceMode == ESharedQuestAcceptance::LeaderAccept",
                    ClampMin=0.0f))
    float LeaderAcceptGraceSeconds = 10.0f;

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        // Same asset type as base — registry loads both identically.
        return FPrimaryAssetId(TEXT("QuestDefinition"), GetFName());
    }
};
```

> **Design note:** Group size constraints (minimum/maximum party size) are expressed as `URequirement_GroupSize` in `UnlockRequirements`, evaluated through the normal requirement system. `USharedQuestDefinition` does not duplicate this logic. Designers add or omit a group size requirement as they see fit per quest.

---

## `UQuestMarkerDataAsset`

**File:** `Quest/Data/QuestMarkerDataAsset.h / .cpp`

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UQuestMarkerDataAsset : public UDataAsset
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
