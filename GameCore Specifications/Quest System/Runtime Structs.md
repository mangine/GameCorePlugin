# Runtime Structs

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

These structs carry per-player mutable quest state. They are replicated, serialized, and injected into `FRequirementContext` at evaluation time. They never live in `UQuestDefinition`.

---

## `FQuestTrackerEntry`

**File:** `Quest/Runtime/QuestRuntime.h`

```cpp
// One progress counter for a single tracker within an active quest stage.
// Incremented server-side only via GMS event handlers in UQuestComponent.
USTRUCT(BlueprintType)
struct PIRATEQUESTS_API FQuestTrackerEntry
{
    GENERATED_BODY()

    // Matches FQuestProgressTrackerDef::TrackerKey in the stage definition.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag TrackerKey;

    // Current accumulated value. Always >= 0.
    // Clamped to EffectiveTarget on de-scaling snapshot (party leave).
    UPROPERTY(BlueprintReadOnly)
    int32 CurrentValue = 0;

    // Cached effective target for this player/party configuration.
    // Recomputed when party size changes. Stored here so the client
    // can display progress without loading the definition asset.
    UPROPERTY(BlueprintReadOnly)
    int32 EffectiveTarget = 1;
};
```

---

## `FQuestRuntime`

**File:** `Quest/Runtime/QuestRuntime.h`

`FQuestRuntime` is the per-player instance of one active quest. It is an element of `UQuestComponent::ActiveQuests`, which uses `FFastArraySerializer` for efficient delta replication.

```cpp
USTRUCT(BlueprintType)
struct PIRATEQUESTS_API FQuestRuntime : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Matches UQuestDefinition::QuestId.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag QuestId;

    // Current stage. Matches a FGameplayTag state in the UStateMachineAsset.
    // Replicated. Client uses this for UI stage display.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag CurrentStageTag;

    // Progress counters for the current stage.
    // Only entries with bReEvaluateOnly=false are present here.
    // Replicated. Client reads these for progress bar display.
    UPROPERTY(BlueprintReadOnly)
    TArray<FQuestTrackerEntry> Trackers;

    // Role this player has in this quest run.
    UPROPERTY(BlueprintReadOnly)
    EQuestMemberRole MemberRole = EQuestMemberRole::Primary;

    // Unix timestamp (seconds) of last completion.
    // Used by URequirement_QuestCooldown and cadence reset checks.
    // 0 = never completed.
    UPROPERTY(BlueprintReadOnly)
    int64 LastCompletedTimestamp = 0;

    // ── FFastArraySerializer callbacks ────────────────────────────────────────
    // Called on the client when this entry is added, updated, or removed.
    // UQuestComponent uses these to trigger UI events.
    void PostReplicatedAdd(const struct FQuestRuntimeArray& Array);
    void PostReplicatedChange(const struct FQuestRuntimeArray& Array);
    void PreReplicatedRemove(const struct FQuestRuntimeArray& Array);

    // ── Helpers ───────────────────────────────────────────────────────────────

    FQuestTrackerEntry* FindTracker(const FGameplayTag& TrackerKey)
    {
        return Trackers.FindByPredicate(
            [&](const FQuestTrackerEntry& E){ return E.TrackerKey == TrackerKey; });
    }

    const FQuestTrackerEntry* FindTracker(const FGameplayTag& TrackerKey) const
    {
        return Trackers.FindByPredicate(
            [&](const FQuestTrackerEntry& E){ return E.TrackerKey == TrackerKey; });
    }
};

// FastArray container. Lives as a UPROPERTY in UQuestComponent.
USTRUCT()
struct PIRATEQUESTS_API FQuestRuntimeArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FQuestRuntime> Items;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<
            FQuestRuntime, FQuestRuntimeArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FQuestRuntimeArray>
    : public TStructOpsTypeTraitsBase2<FQuestRuntimeArray>
{
    enum { WithNetDeltaSerializer = true };
};
```

---

## Replication Policy

| Field | Replicated | Notes |
|---|---|---|
| `ActiveQuests` | Yes — FFastArraySerializer | Delta replicated per-item to owning client only |
| `CompletedQuestTags` | Yes — `ReplicatedUsing` | Full tag container, changes infrequent |
| `FQuestTrackerEntry::CurrentValue` | Yes (part of FQuestRuntime) | Client uses for progress bars |
| `FQuestTrackerEntry::EffectiveTarget` | Yes (part of FQuestRuntime) | Needed client-side for progress display |
| `LastCompletedTimestamp` | Yes (part of FQuestRuntime) | Client shows cooldown countdown |

> **Owning client only.** Quest runtime data is replicated only to the player who owns it. No other client sees another player's quest state. Party progress is shown via a separate party HUD channel, not by replicating FQuestRuntime to all party members.

---

## Building `FRequirementContext` from `FQuestRuntime`

This is how `UQuestComponent` injects tracker data into the requirement context before evaluation:

```cpp
FRequirementContext UQuestComponent::BuildRequirementContext(
    const FQuestRuntime& QuestRuntime,
    const UQuestDefinition* Definition) const
{
    FRequirementContext Ctx;
    Ctx.PlayerState = GetOwner<APlayerState>();
    Ctx.World       = GetWorld();
    Ctx.Instigator  = Ctx.PlayerState
        ? Ctx.PlayerState->GetPawn() : nullptr;

    // Build one payload per stage, keyed by a domain tag.
    // Convention: Quest.Payload.<QuestName>.<StageName>
    // For simplicity, one payload per active quest keyed by QuestId.
    FRequirementPayload Payload;
    for (const FQuestTrackerEntry& Tracker : QuestRuntime.Trackers)
    {
        Payload.Counters.Add(Tracker.TrackerKey, Tracker.CurrentValue);
    }

    // Also expose LastCompletedTimestamp as a float for cooldown requirements.
    if (QuestRuntime.LastCompletedTimestamp > 0)
    {
        Payload.Floats.Add(
            FGameplayTag::RequestGameplayTag(TEXT("Quest.Counter.LastCompleted")),
            static_cast<float>(QuestRuntime.LastCompletedTimestamp));
    }

    Ctx.PersistedData.Add(QuestRuntime.QuestId, Payload);
    return Ctx;
}
```

> **Note:** The payload key is `QuestId`. `URequirement_Persisted` instances on this quest's requirement lists must have `PayloadKey` set to the same `QuestId` tag. This is validated in `UQuestDefinition::IsDataValid`.

---

## Persistence Serialization

`UQuestComponent` implements `IPersistableComponent`. Serialization format:

```cpp
void UQuestComponent::Serialize_Save(FArchive& Ar)
{
    // CompletedQuestTags — serialized as array of tag names
    TArray<FName> TagNames;
    CompletedQuestTags.GetGameplayTagArray().ForEach(
        [&](const FGameplayTag& T){ TagNames.Add(T.GetTagName()); });
    Ar << TagNames;

    // ActiveQuests
    int32 Count = ActiveQuests.Items.Num();
    Ar << Count;
    for (FQuestRuntime& Q : ActiveQuests.Items)
    {
        FName QuestIdName = Q.QuestId.GetTagName();
        FName StageTagName = Q.CurrentStageTag.GetTagName();
        Ar << QuestIdName << StageTagName;
        Ar << Q.MemberRole;
        Ar << Q.LastCompletedTimestamp;

        int32 TrackerCount = Q.Trackers.Num();
        Ar << TrackerCount;
        for (FQuestTrackerEntry& T : Q.Trackers)
        {
            FName TrackerKeyName = T.TrackerKey.GetTagName();
            Ar << TrackerKeyName << T.CurrentValue << T.EffectiveTarget;
        }
    }
}
```

> Tags are serialized as `FName` (tag name string) rather than raw indices for forward compatibility across tag list changes.
