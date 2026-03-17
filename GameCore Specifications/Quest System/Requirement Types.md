# Requirement Types

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**Module:** `PirateGame.Quest` (all types here are quest-module specific — none live in GameCore)

All types here inherit from GameCore base classes. They live under `Quest/Requirements/`.

---

## `URequirement_KillCount`

**File:** `Quest/Requirements/Requirement_KillCount.h / .cpp` 
**Inherits:** `URequirement_Persisted` (GameCore)

```cpp
// Passes when a kill counter in the payload meets or exceeds RequiredKills.
// PayloadKey must match the QuestId tag used to key the payload in FRequirementContext.
// CounterTag must match the FQuestProgressTrackerDef::TrackerKey for the kill tracker.
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Kill Count"))
class PIRATEQUESTS_API URequirement_KillCount : public URequirement_Persisted
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement", meta=(ClampMin=1))
    int32 RequiredKills = 1;

    // Matches FQuestProgressTrackerDef::TrackerKey.
    // Convention: Quest.Counter.Kill.<MobType> or Quest.Counter.Kill.Any
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Counter"))
    FGameplayTag CounterTag;

    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const override
    {
        int32 Current = 0;
        Payload.GetCounter(CounterTag, Current);

        if (Current >= RequiredKills)
            return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            FText::Format(
                LOCTEXT("NeedMoreKills", "{0} / {1} kills"),
                FText::AsNumber(Current),
                FText::AsNumber(RequiredKills)));
    }

    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag(
                TEXT("RequirementEvent.Quest.TrackerUpdated")));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Kill Count: %d (counter: %s)"),
            RequiredKills, *CounterTag.ToString());
    }
#endif
};
```

---

## `URequirement_QuestCompleted`

**File:** `Quest/Requirements/Requirement_QuestCompleted.h / .cpp` 
**Inherits:** `URequirement` (GameCore, direct) 
**Authority:** `Both` (CompletedQuestTags is replicated — client can evaluate)

```cpp
// Passes when the player's CompletedQuestTags contains the specified tag.
// bIsMonotonic = true: once completed, never un-completes (except on Evergreen cadence reset,
// which is handled by the quest system removing the tag, not by this requirement).
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
        if (!Context.PlayerState) return FRequirementResult::Fail(
            LOCTEXT("NoPlayerState", "No player state."));

        UQuestComponent* QC =
            Context.PlayerState->FindComponentByClass<UQuestComponent>();
        if (!QC) return FRequirementResult::Fail(
            LOCTEXT("NoQuestComp", "No quest component."));

        if (QC->CompletedQuestTags.HasTag(RequiredQuestCompletedTag))
            return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            LOCTEXT("QuestNotCompleted", "Required quest not yet completed."));
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
**Authority:** `Both` (`LastCompletedTimestamp` is in replicated `FQuestRuntime`)

```cpp
// Passes when enough time has elapsed since last completion, OR when the
// relevant cadence reset has occurred since last completion.
// PayloadKey = QuestId. Reads Floats[Quest.Counter.LastCompleted].
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Cooldown"))
class PIRATEQUESTS_API URequirement_QuestCooldown : public URequirement_Persisted
{
    GENERATED_BODY()
public:
    // Cadence mode. If None, CooldownSeconds is used.
    UPROPERTY(EditDefaultsOnly, Category="Requirement")
    EQuestResetCadence Cadence = EQuestResetCadence::None;

    // Elapsed seconds required since last completion. Only used when Cadence == None.
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(EditCondition="Cadence == EQuestResetCadence::None",
                    ClampMin=0.0f))
    float CooldownSeconds = 86400.0f; // 24h default

    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const override
    {
        float LastCompleted = 0.0f;
        const bool bHasTimestamp = Payload.GetFloat(
            FGameplayTag::RequestGameplayTag(
                TEXT("Quest.Counter.LastCompleted")),
            LastCompleted);

        if (!bHasTimestamp || LastCompleted <= 0.0f)
            return FRequirementResult::Pass(); // Never completed — no cooldown

        const int64 LastCompletedTs = static_cast<int64>(LastCompleted);
        const int64 NowTs = FDateTime::UtcNow().ToUnixTimestamp();

        if (Cadence == EQuestResetCadence::None)
        {
            if ((NowTs - LastCompletedTs) >= static_cast<int64>(CooldownSeconds))
                return FRequirementResult::Pass();

            const int64 Remaining = static_cast<int64>(CooldownSeconds)
                - (NowTs - LastCompletedTs);
            return FRequirementResult::Fail(
                FText::Format(
                    LOCTEXT("OnCooldown", "Available in {0}s"),
                    FText::AsNumber(Remaining)));
        }

        // For Daily/Weekly: get the last reset timestamp from the registry subsystem.
        // On client: the registry subsystem also runs (read-only). Reset timestamps
        // are replicated via GameState or a separate channel if client accuracy is needed.
        // For client-side evaluation, we use the locally computed reset timestamp.
        const UQuestRegistrySubsystem* Registry =
            Context.World
                ? Context.World->GetSubsystem<UQuestRegistrySubsystem>()
                : nullptr;

        if (!Registry)
            return FRequirementResult::Pass(); // Fail-safe: pass if no registry

        int64 LastReset = 0;
        if (Cadence == EQuestResetCadence::Daily)
            LastReset = Registry->GetLastDailyResetTimestamp();
        else if (Cadence == EQuestResetCadence::Weekly)
            LastReset = Registry->GetLastWeeklyResetTimestamp();

        if (LastCompletedTs < LastReset)
            return FRequirementResult::Pass(); // Reset occurred after last completion

        return FRequirementResult::Fail(
            LOCTEXT("NotYetReset", "Quest resets daily at 00:00 UTC."));
    }

    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        // Invalidated when quest completes or cadence resets
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag(
                TEXT("RequirementEvent.Quest.Completed")));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        if (Cadence == EQuestResetCadence::None)
            return FString::Printf(TEXT("Cooldown: %.0fs"), CooldownSeconds);
        return FString::Printf(TEXT("Cadence Reset: %s"),
            *UEnum::GetDisplayValueAsText(Cadence).ToString());
    }
#endif
};
```

---

## `URequirement_GroupSize`

**File:** `Quest/Requirements/Requirement_GroupSize.h / .cpp` 
**Inherits:** `URequirement` (GameCore, direct) 
**Authority:** `ServerOnly` (party size is server-authoritative)

```cpp
// Passes when the player's current party size is within [MinSize, MaxSize].
// Used to gate GroupRequired and GroupOnly quests.
// Must only appear in ServerOnly requirement lists.
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Group Size"))
class PIRATEQUESTS_API URequirement_GroupSize : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement", meta=(ClampMin=1))
    int32 MinSize = 2;

    UPROPERTY(EditDefaultsOnly, Category="Requirement", meta=(ClampMin=1))
    int32 MaxSize = 5;

    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        return ERequirementDataAuthority::ServerOnly;
    }

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        // Retrieve party size via game-specific party subsystem.
        // URequirement does not depend on the party system directly —
        // the party size is read from the PlayerState via an interface.
        IPartyMemberInterface* PartyMember =
            Cast<IPartyMemberInterface>(Context.PlayerState);
        if (!PartyMember)
            return FRequirementResult::Fail(
                LOCTEXT("NotInParty", "Must be in a party."));

        const int32 PartySize = PartyMember->GetCurrentPartySize();
        if (PartySize >= MinSize && PartySize <= MaxSize)
            return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            FText::Format(
                LOCTEXT("WrongPartySize", "Party size must be {0}–{1}. Current: {2}"),
                FText::AsNumber(MinSize),
                FText::AsNumber(MaxSize),
                FText::AsNumber(PartySize)));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Group Size: %d–%d"), MinSize, MaxSize);
    }
#endif
};
```

---

## `URequirement_ActiveQuestCount`

**File:** `Quest/Requirements/Requirement_ActiveQuestCount.h` 
**Inherits:** `URequirement` (GameCore, direct) 
**Authority:** `Both`

```cpp
// Passes when the player has fewer than MaxActiveQuests active quests.
// Used internally by UQuestComponent — can also be placed in unlock requirements
// to prevent a quest from becoming Available if the player is already at capacity.
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
        if (!Context.PlayerState) return FRequirementResult::Fail(
            LOCTEXT("NoPS", "No player state."));

        UQuestComponent* QC =
            Context.PlayerState->FindComponentByClass<UQuestComponent>();
        if (!QC) return FRequirementResult::Pass();

        if (QC->ActiveQuests.Items.Num() < MaxAllowed)
            return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            LOCTEXT("AtCapacity", "Quest log is full."));
    }
};
```
