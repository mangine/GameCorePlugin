# Quest-Owned Requirement Types

**Module:** Quest module (game-side)

Only requirements whose data is **owned by the quest system** are defined here. Requirements for combat kills, inventory checks, or group size live in their respective modules.

> **Ownership rule:** A requirement type belongs to the module that owns its data source.
> - Quest completion state → quest system (`CompletedQuestTags`)
> - Quest cooldown timestamps → quest system (`LastCompletedTimestamp`)
> - Active quest count → quest system (`ActiveQuests`)
> - Kill counts → game module integration layer
> - Group size → party/group system module

---

## `URequirement_QuestCompleted`

**File:** `Quest/Requirements/Requirement_QuestCompleted.h / .cpp`
**Inherits:** `URequirement`
**`bIsMonotonic`:** `true` — completion is permanent (for singleton quests)

```cpp
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Completed"))
class YOURGAME_API URequirement_QuestCompleted : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Completed"))
    FGameplayTag RequiredQuestCompletedTag;

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        const FQuestEvaluationContext* CtxData =
            Context.Data.GetPtr<FQuestEvaluationContext>();
        if (!CtxData || !CtxData->PlayerState)
            return FRequirementResult::Fail(LOCTEXT("NoCtx", "No player context."));

        const UQuestComponent* QC =
            CtxData->PlayerState->FindComponentByClass<UQuestComponent>();
        if (!QC)
            return FRequirementResult::Fail(LOCTEXT("NoQC", "No quest component."));

        return QC->CompletedQuestTags.HasTag(RequiredQuestCompletedTag)
            ? FRequirementResult::Pass()
            : FRequirementResult::Fail(
                LOCTEXT("NotDone", "Required quest not yet completed."));
    }

    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(TAG_RequirementEvent_Quest_Completed);
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Quest Completed: %s"),
            *RequiredQuestCompletedTag.ToString());
    }
#endif
};
```

---

## `URequirement_QuestCooldown`

**File:** `Quest/Requirements/Requirement_QuestCooldown.h / .cpp`
**Inherits:** `URequirement`

> Reads `LastCompletedTimestamp` directly from `UQuestComponent` via `FindComponentByClass`. Not a `URequirement_Persisted` subclass — `LastCompletedTimestamp` is `int64` and the persisted payload stores counters. Casting directly avoids precision loss.

```cpp
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Cooldown"))
class YOURGAME_API URequirement_QuestCooldown : public URequirement
{
    GENERATED_BODY()
public:
    // Must match the QuestId this cooldown gates.
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Id"))
    FGameplayTag QuestIdKey;

    UPROPERTY(EditDefaultsOnly, Category="Requirement")
    EQuestResetCadence Cadence = EQuestResetCadence::None;

    // Only used when Cadence == None.
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(EditCondition="Cadence == EQuestResetCadence::None",
                    ClampMin=0.0f))
    float CooldownSeconds = 86400.0f;

    virtual ERequirementDataAuthority GetDataAuthority() const override
    { return ERequirementDataAuthority::Both; }

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        const FQuestEvaluationContext* CtxData =
            Context.Data.GetPtr<FQuestEvaluationContext>();
        if (!CtxData || !CtxData->PlayerState)
            return FRequirementResult::Pass(); // no context → pass silently

        const UQuestComponent* QC =
            CtxData->PlayerState->FindComponentByClass<UQuestComponent>();
        if (!QC) return FRequirementResult::Pass();

        // LastCompletedTimestamp is in FQuestRuntime (if quest was ever active)
        // or 0 if it was never completed.
        const FQuestRuntime* Runtime = QC->FindActiveQuest(QuestIdKey);
        const int64 LastCompleted = Runtime ? Runtime->LastCompletedTimestamp : 0;
        if (LastCompleted <= 0) return FRequirementResult::Pass();

        const int64 NowTs = FDateTime::UtcNow().ToUnixTimestamp();

        if (Cadence == EQuestResetCadence::None)
        {
            const int64 Required = static_cast<int64>(CooldownSeconds);
            const int64 Elapsed  = NowTs - LastCompleted;
            if (Elapsed >= Required) return FRequirementResult::Pass();
            return FRequirementResult::Fail(
                FText::Format(
                    LOCTEXT("Cooldown", "Available in {0}s"),
                    FText::AsNumber(Required - Elapsed)));
        }

        // Cadence-based: check against last reset timestamp.
        const UQuestRegistrySubsystem* Registry =
            CtxData->World
                ? CtxData->World->GetGameInstance()
                      ->GetSubsystem<UQuestRegistrySubsystem>()
                : nullptr;
        if (!Registry) return FRequirementResult::Pass();

        int64 LastReset = 0;
        if (Cadence == EQuestResetCadence::Daily)
            LastReset = Registry->GetLastDailyResetTimestamp();
        else if (Cadence == EQuestResetCadence::Weekly)
            LastReset = Registry->GetLastWeeklyResetTimestamp();

        if (LastCompleted < LastReset) return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            LOCTEXT("NotReset", "Quest has not yet reset."));
    }

    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(TAG_RequirementEvent_Quest_Completed);
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        if (Cadence == EQuestResetCadence::None)
            return FString::Printf(TEXT("Cooldown: %.0fs"), CooldownSeconds);
        return FString::Printf(TEXT("Cadence: %s"),
            *UEnum::GetDisplayValueAsText(Cadence).ToString());
    }
#endif
};
```

### UI Cooldown Countdown Pattern

```
Cadence::None:   NextAvailable = LastCompletedTimestamp + CooldownSeconds
Cadence::Daily:  NextAvailable = GetLastDailyResetTimestamp() + 86400
Cadence::Weekly: NextAvailable = GetLastWeeklyResetTimestamp() + 604800
Remaining = NextAvailable - FDateTime::UtcNow().ToUnixTimestamp()
```

Subscribe to `GameCoreEvent.Quest.DailyReset` / `WeeklyReset` to refresh the display on reset.

---

## `URequirement_ActiveQuestCount`

**File:** `Quest/Requirements/Requirement_ActiveQuestCount.h`
**Inherits:** `URequirement`

```cpp
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Active Quest Capacity"))
class YOURGAME_API URequirement_ActiveQuestCount : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement", meta=(ClampMin=1))
    int32 MaxAllowed = 20;

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        const FQuestEvaluationContext* CtxData =
            Context.Data.GetPtr<FQuestEvaluationContext>();
        if (!CtxData || !CtxData->PlayerState)
            return FRequirementResult::Pass();

        const UQuestComponent* QC =
            CtxData->PlayerState->FindComponentByClass<UQuestComponent>();
        if (!QC) return FRequirementResult::Pass();

        return QC->ActiveQuests.Items.Num() < MaxAllowed
            ? FRequirementResult::Pass()
            : FRequirementResult::Fail(LOCTEXT("AtCap", "Quest log is full."));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Active Quests < %d"), MaxAllowed);
    }
#endif
};
```

---

## Tracker-Based Requirements (Stub — Game Module)

Kill-count and gathering requirements live in the **game module** integration layer. They do not belong in the quest module. Pattern:

```
Game module: URequirement_KillCount : public URequirement
  Reads:         UQuestComponent tracker value via FindComponentByClass
  QuestIdKey:    FGameplayTag — the quest this counter belongs to
  TrackerKey:    FGameplayTag — Quest.Counter.Kill.<MobType>
  WatchedEvents: RequirementEvent.Quest.TrackerUpdated
  Evaluate:      find active quest runtime, find tracker entry, compare CurrentValue >= Target
```

## Group Size Requirements (Stub — Party/Group Module)

```
Party module: URequirement_GroupSize : public URequirement
  Reads:         IGroupProvider::GetGroupSize() from Context PlayerState
  Authority:     ServerOnly
  WatchedEvents: RequirementEvent.Group.MemberCountChanged
```
