# Requirement Types

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**Module:** `PirateGame.Quest` (all types here are quest-module specific)

Only requirements whose data is **owned by the quest system** are defined here. Requirements that depend on data from other systems (combat, inventory, group/party) are defined in those systems or in game-module integration layers.

---

## Ownership Rule

> A requirement type belongs to the module that owns its data source.
> - Kill counts → combat/integration layer (reads `FRequirementPayload` via `URequirement_Persisted`)
> - Group size → party/group system (reads `IGroupProvider`)
> - Quest completion state → quest system (`CompletedQuestTags` on `UQuestComponent`)
> - Quest cooldown → quest system (`LastCompletedTimestamp` on `FQuestRuntime`)
> - Active quest count → quest system (`ActiveQuests` on `UQuestComponent`)

---

## `URequirement_QuestCompleted`

**File:** `Quest/Requirements/Requirement_QuestCompleted.h / .cpp` 
**Inherits:** `URequirement` (GameCore) 
**Authority:** `Both` — `CompletedQuestTags` is replicated, client can evaluate 
**`bIsMonotonic`:** `true` by default — completion never un-completes (cadence resets are handled by the quest system removing the tag, not by this requirement)

```cpp
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Completed"))
class PIRATEQUESTS_API URequirement_QuestCompleted : public URequirement
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
        if (!Context.PlayerState)
            return FRequirementResult::Fail(
                LOCTEXT("NoPS", "No player state."));

        const UQuestComponent* QC =
            Context.PlayerState->FindComponentByClass<UQuestComponent>();
        if (!QC)
            return FRequirementResult::Fail(
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
**Inherits:** `URequirement_Persisted` (GameCore) 
**Authority:** `Both` — `LastCompletedTimestamp` is in replicated `FQuestRuntime` 
**PayloadKey:** Must match `QuestId` (the domain key used by `UQuestComponent::BuildRequirementContext`)

```cpp
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Cooldown"))
class PIRATEQUESTS_API URequirement_QuestCooldown : public URequirement_Persisted
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement")
    EQuestResetCadence Cadence = EQuestResetCadence::None;

    // Used only when Cadence == None. Seconds elapsed since last completion.
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(EditCondition="Cadence == EQuestResetCadence::None",
                    ClampMin=0.0f))
    float CooldownSeconds = 86400.0f;

    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const override
    {
        float LastCompleted = 0.0f;
        const bool bHasTs = Payload.GetFloat(
            FGameplayTag::RequestGameplayTag(
                TEXT("Quest.Counter.LastCompleted")),
            LastCompleted);

        if (!bHasTs || LastCompleted <= 0.0f)
            return FRequirementResult::Pass(); // Never completed, no cooldown

        const int64 LastTs = static_cast<int64>(LastCompleted);
        const int64 NowTs  = FDateTime::UtcNow().ToUnixTimestamp();

        if (Cadence == EQuestResetCadence::None)
        {
            const int64 Elapsed   = NowTs - LastTs;
            const int64 Required  = static_cast<int64>(CooldownSeconds);
            if (Elapsed >= Required) return FRequirementResult::Pass();

            return FRequirementResult::Fail(
                FText::Format(
                    LOCTEXT("Cooldown", "Available in {0}s"),
                    FText::AsNumber(Required - Elapsed)));
        }

        const UQuestRegistrySubsystem* Registry =
            Context.World
                ? Context.World->GetSubsystem<UQuestRegistrySubsystem>()
                : nullptr;
        if (!Registry) return FRequirementResult::Pass();

        int64 LastReset = 0;
        if      (Cadence == EQuestResetCadence::Daily)
            LastReset = Registry->GetLastDailyResetTimestamp();
        else if (Cadence == EQuestResetCadence::Weekly)
            LastReset = Registry->GetLastWeeklyResetTimestamp();

        if (LastTs < LastReset) return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            LOCTEXT("NotReset", "Quest resets daily at 00:00 UTC."));
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

---

## `URequirement_ActiveQuestCount`

**File:** `Quest/Requirements/Requirement_ActiveQuestCount.h` 
**Inherits:** `URequirement` (GameCore) 
**Authority:** `Both`

```cpp
// Passes when the player has fewer than MaxAllowed active quests.
// Can be placed in UnlockRequirements to prevent a quest appearing
// as Available when the player is already at capacity.
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Active Quest Capacity"))
class PIRATEQUESTS_API URequirement_ActiveQuestCount : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement", meta=(ClampMin=1))
    int32 MaxAllowed = 20;

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        if (!Context.PlayerState)
            return FRequirementResult::Fail(
                LOCTEXT("NoPS", "No player state."));

        const UQuestComponent* QC =
            Context.PlayerState->FindComponentByClass<UQuestComponent>();
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

Requirements that count game events (kills, resource gathers, interactions) are defined in the **game module integration layer**, not in the quest system. They inherit `URequirement_Persisted` (GameCore) and read `FRequirementPayload::Counters` injected by `UQuestComponent`'s watcher `ContextBuilder`.

```
Game module: URequirement_KillCount : public URequirement_Persisted
  PayloadKey  = QuestId tag
  CounterTag  = Quest.Counter.Kill.<MobType> (or Quest.Counter.Kill.Any)
  Reads       = Payload.Counters[CounterTag]
  WatchedEvents = RequirementEvent.Quest.TrackerUpdated
```

The tracker value itself is incremented by an external bridge component that subscribes to `GameCoreEvent.Combat.MobKilled` and calls `UQuestComponent::Server_IncrementTracker`. The requirement only reads the result.

### Group Size Requirements

Requirements that gate on group membership size belong to the **party/group system module**. They will be specced when a concrete group system is implemented. They read data via `IGroupProvider` (GameCore) and carry `GetDataAuthority() == ServerOnly`.

```
Group system module: URequirement_GroupSize : public URequirement
  Reads      = IGroupProvider::GetGroupSize() from Context.PlayerState
  Authority  = ServerOnly
  Lives in   = Party/Group system module, NOT quest module
```
