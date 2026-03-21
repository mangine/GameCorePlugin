# Quest Data Definitions

All types on this page are pure data — no mutable runtime state. They are loaded by `UQuestRegistrySubsystem` on demand and shared across all players.

---

## `FQuestDisplayData`

**File:** `Quest/Data/QuestDisplayData.h`

```cpp
USTRUCT(BlueprintType)
struct YOURGAME_API FQuestDisplayData
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

Defines a single counter for a quest stage. Stored in `UQuestStageDefinition::Trackers`.

```cpp
USTRUCT(BlueprintType)
struct YOURGAME_API FQuestProgressTrackerDef
{
    GENERATED_BODY()

    // Unique key for this tracker within the stage.
    // Convention: Quest.Counter.<Domain>.<Target> (e.g. Quest.Counter.Kill.Crab)
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
              meta=(Categories="Quest.Counter"))
    FGameplayTag TrackerKey;

    // Target value for solo play (GroupSize == 1).
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin=1))
    int32 TargetValue = 1;

    // Per-additional-member contribution multiplier.
    // 0.0 = non-scalable (target stays fixed regardless of group size).
    // 0.5 = each extra member adds 50% of TargetValue to the shared target.
    // EffectiveTarget = TargetValue + (GroupSize - 1) * TargetValue * ScalingMultiplier
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
              meta=(ClampMin=0.0f, ClampMax=1.0f))
    float ScalingMultiplier = 0.0f;

    // If true: no FQuestTrackerEntry is created. CompletionRequirements
    // re-evaluate from live world state on every check. Use for inventory,
    // zone, or equipment conditions.
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

Defines one node (state) in the quest stage graph. Instanced inside `UQuestDefinition::Stages`.

```cpp
UCLASS(EditInlineNew, CollapseCategories, BlueprintType)
class YOURGAME_API UQuestStageDefinition : public UObject
{
    GENERATED_BODY()
public:
    // Must match a state tag in UQuestDefinition::StageGraph.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FGameplayTag StageTag;

    // Requirements that must all pass for the stage to be considered complete.
    // Evaluated imperatively when any tracker hits EffectiveTarget.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    TObjectPtr<URequirementList> CompletionRequirements;

    // Progress counters for this stage. bReEvaluateOnly entries are omitted
    // from FQuestRuntime::Trackers — they have no persistent counter.
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="Stage")
    TArray<FQuestProgressTrackerDef> Trackers;

    // Localised objective text. Broadcast in FQuestStageChangedPayload.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    FText StageObjectiveText;

    // This stage entering means the quest has been completed successfully.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    bool bIsCompletionState = false;

    // This stage entering means the quest has failed.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stage")
    bool bIsFailureState = false;

    const FQuestProgressTrackerDef* FindTrackerDef(const FGameplayTag& Key) const
    {
        return Trackers.FindByPredicate(
            [&](const FQuestProgressTrackerDef& T){ return T.TrackerKey == Key; });
    }

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const;
#endif
};
```

> **Note:** `bIsCompletionState` and `bIsFailureState` mirror the flags on `UQuestStateNode` (the `UStateNodeBase` subclass used in the stage graph asset). Both must be consistent — `IsDataValid` enforces this.

---

## `UQuestDefinition`

**File:** `Quest/Data/QuestDefinition.h / .cpp`

Base class for solo quests. No group or sharing concepts.

```cpp
UCLASS(BlueprintType)
class YOURGAME_API UQuestDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // ── Identity ────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Id"))
    FGameplayTag QuestId;

    // Tag added to CompletedQuestTags on permanent close.
    // For SingleAttempt: added on complete AND fail.
    // For RetryUntilComplete: added on complete only.
    // For RetryAndAssist / Evergreen: never added (quest remains available).
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Completed"))
    FGameplayTag QuestCompletedTag;

    // ── Live-ops ───────────────────────────────────────────────────────────

    // Kill switch. Disabled quests are excluded from candidate unlock lists.
    // Active disabled quests are removed on login WITHOUT adding QuestCompletedTag.
    // Re-enabling makes the quest immediately available again on next login.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    bool bEnabled = true;

    // ── Lifecycle & Rules ──────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestLifecycle Lifecycle = EQuestLifecycle::RetryUntilComplete;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestCheckAuthority CheckAuthority = EQuestCheckAuthority::ClientValidated;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestResetCadence ResetCadence = EQuestResetCadence::None;

    // Unix timestamp (int64). 0 = no expiry. Only meaningful for EventBound cadence.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    int64 ExpiryTimestamp = 0;

    // ── Stage Graph ─────────────────────────────────────────────────────────

    // UStateMachineAsset drives stage transition logic.
    // UQuestComponent reads this directly via FindFirstPassingTransition.
    // UStateMachineComponent is NOT added to APlayerState.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Stages")
    TObjectPtr<UStateMachineAsset> StageGraph;

    // Stage definitions — one per state node in StageGraph.
    // Looked up by StageTag at runtime.
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="Quest|Stages")
    TArray<TObjectPtr<UQuestStageDefinition>> Stages;

    // ── Requirements ────────────────────────────────────────────────────────

    // Requirements evaluated reactively (RegisterWatch) for unlock detection
    // and imperatively as a baseline check at login.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Requirements")
    TObjectPtr<URequirementList> UnlockRequirements;

    // ── Rewards ──────────────────────────────────────────────────────────────

    // Soft reference — loaded and granted by the reward system on Quest.Completed.
    // The quest system never loads these.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> FirstTimeRewardTable;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> RepeatingRewardTable;

    // ── Categorisation & Display ──────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display",
              meta=(Categories="Quest.Marker"))
    FGameplayTag QuestMarkerTag;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FGameplayTagContainer QuestCategories;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FQuestDisplayData Display;

    // ── API ──────────────────────────────────────────────────────────────────

    const UQuestStageDefinition* FindStage(const FGameplayTag& StageTag) const
    {
        const TObjectPtr<UQuestStageDefinition>* Found =
            Stages.FindByPredicate(
                [&](const TObjectPtr<UQuestStageDefinition>& S)
                { return S && S->StageTag == StageTag; });
        return Found ? Found->Get() : nullptr;
    }

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
class YOURGAME_API USharedQuestDefinition : public UQuestDefinition
{
    GENERATED_BODY()
public:

    // How the group collectively accepts this quest.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Group")
    ESharedQuestAcceptance AcceptanceMode = ESharedQuestAcceptance::IndividualAccept;

    // Grace window passed to OnRequestGroupEnrollment on the coordinator.
    // Only meaningful for LeaderAccept. The group system owns this timer.
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

> **Group size constraints** (minimum/maximum party size) are expressed as `URequirement_GroupSize` in `UnlockRequirements`, not as fields here.

---

## `UQuestConfigDataAsset`

**File:** `Quest/Data/QuestConfigDataAsset.h`

Externalizes tunables from `UQuestComponent`. Assign a `UQuestConfigDataAsset` to `UQuestComponent::QuestConfig` to avoid recompiling for tuning changes.

```cpp
UCLASS(BlueprintType)
class YOURGAME_API UQuestConfigDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    // Server-enforced. Client reads ActiveQuests.Items.Num() for pre-validation UI hints.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(ClampMin=1, ClampMax=200))
    int32 MaxActiveQuests = 20;
};
```

---

## `UQuestMarkerDataAsset`

**File:** `Quest/Data/QuestMarkerDataAsset.h / .cpp`

Maps quest marker tags to icons. Loaded by the UI — never by the quest system.

```cpp
UCLASS(BlueprintType)
class YOURGAME_API UQuestMarkerDataAsset : public UDataAsset
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
