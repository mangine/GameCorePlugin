# Requirement Types

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**Module:** `GameCore` (quest module within the plugin)

Only requirements whose data is **owned by the quest system** are defined here. Requirements that depend on data from other systems (combat, inventory, group/party) are defined in those systems or in game-module integration layers.

---

## Ownership Rule

> A requirement type belongs to the module that owns its data source.
> - Kill counts → game module integration layer
> - Group size → party/group system module
> - Quest completion state → quest system (`CompletedQuestTags`)
> - Quest cooldown → quest system (`LastCompletedTimestamp`)
> - Active quest count → quest system (`ActiveQuests`)

---

## `URequirement_QuestCompleted`

**File:** `Quest/Requirements/Requirement_QuestCompleted.h / .cpp` 
**Inherits:** `URequirement` 
**Authority:** `Both` — `CompletedQuestTags` is replicated 
**`bIsMonotonic`:** `true` — completion never un-completes

```cpp
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Completed"))
class GAMECORE_API URequirement_QuestCompleted : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Completed"))
    FGameplayTag RequiredQuestCompletedTag;

    UPROPERTY(EditDefaultsOnly, Category="Requirement")
    bool bIsMonotonic = true;

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        // Use cached QuestComponent pointer — avoids FindComponentByClass.
        const UQuestComponent* QC = Context.QuestComponent;
        if (!QC)
        {
            if (!Context.PlayerState) return FRequirementResult::Fail(
                LOCTEXT("NoPS", "No player state."));
            QC = Context.PlayerState->FindComponentByClass<UQuestComponent>();
        }
        if (!QC) return FRequirementResult::Fail(
            LOCTEXT("NoQC", "No quest component on player state."));

        if (QC->CompletedQuestTags.HasTag(RequiredQuestCompletedTag))
            return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            LOCTEXT("NotDone", "Required quest not yet completed."));
    }

    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag(
                TEXT("RequirementEvent.Quest.Completed")));
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
**Inherits:** `URequirement` (not `URequirement_Persisted` — reads directly from `Context.QuestComponent`) 
**Authority:** `Both` — `LastCompletedTimestamp` is in replicated `FQuestRuntime`

> **Why not `URequirement_Persisted`?** `LastCompletedTimestamp` is `int64`. `FRequirementPayload` stores only `float` values. Reading directly from `Context.QuestComponent->FindActiveQuest(QuestIdKey)` avoids precision loss and simplifies the data path.

```cpp
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Cooldown"))
class GAMECORE_API URequirement_QuestCooldown : public URequirement
{
    GENERATED_BODY()
public:
    // Must match the QuestId of the quest this cooldown gates.
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Id"))
    FGameplayTag QuestIdKey;

    UPROPERTY(EditDefaultsOnly, Category="Requirement")
    EQuestResetCadence Cadence = EQuestResetCadence::None;

    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(EditCondition="Cadence == EQuestResetCadence::None",
                    ClampMin=0.0f))
    float CooldownSeconds = 86400.0f;

    virtual ERequirementDataAuthority GetDataAuthority() const override
    { return ERequirementDataAuthority::Both; }

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        const UQuestComponent* QC = Context.QuestComponent;
        if (!QC && Context.PlayerState)
            QC = Context.PlayerState->FindComponentByClass<UQuestComponent>();
        if (!QC) return FRequirementResult::Pass();

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

        const UQuestRegistrySubsystem* Registry =
            Context.World
                ? Context.World->GetGameInstance()
                      ->GetSubsystem<UQuestRegistrySubsystem>()
                : nullptr;
        if (!Registry) return FRequirementResult::Pass();

        int64 LastReset = 0;
        if      (Cadence == EQuestResetCadence::Daily)
            LastReset = Registry->GetLastDailyResetTimestamp();
        else if (Cadence == EQuestResetCadence::Weekly)
            LastReset = Registry->GetLastWeeklyResetTimestamp();

        if (LastCompleted < LastReset) return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            LOCTEXT("NotReset", "Quest not yet reset."));
    }

    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag(
                TEXT("RequirementEvent.Quest.Completed")));
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
**Authority:** `Both`

```cpp
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Active Quest Capacity"))
class GAMECORE_API URequirement_ActiveQuestCount : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement", meta=(ClampMin=1))
    int32 MaxAllowed = 20;

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        const UQuestComponent* QC = Context.QuestComponent;
        if (!QC && Context.PlayerState)
            QC = Context.PlayerState->FindComponentByClass<UQuestComponent>();
        if (!QC) return FRequirementResult::Pass();

        if (QC->ActiveQuests.Items.Num() < MaxAllowed)
            return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            LOCTEXT("AtCap", "Quest log is full."));
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

## Stub Notes — Requirements Defined Elsewhere

### Tracker-Based Requirements (e.g. Kill Count)

Live in the game module. Inherit `URequirement_Persisted` (GameCore). Read `FRequirementPayload::Counters` via `Context.PersistedData[QuestId]`.

```
Game module: URequirement_KillCount : public URequirement_Persisted
  PayloadKey  = QuestId tag
  CounterTag  = Quest.Counter.Kill.<MobType>
  Reads       = Payload.Counters[CounterTag]
  WatchedEvents = RequirementEvent.Quest.TrackerUpdated
```

### Group Size Requirements

Live in the party/group system module. Read via `IGroupProvider::GetGroupSize()` cast from `Context.PlayerState`. `GetDataAuthority() == ServerOnly`.
