# Runtime Structs

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

These structs carry per-player mutable quest state. They are replicated, serialized, and injected into `FRequirementContext` at evaluation time. They never live in `UQuestDefinition`.

---

## `FQuestTrackerEntry`

**File:** `Quest/Runtime/QuestRuntime.h`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FQuestTrackerEntry
{
    GENERATED_BODY()

    // Matches FQuestProgressTrackerDef::TrackerKey in the stage definition.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag TrackerKey;

    // Current accumulated value. Always >= 0. Clamped to EffectiveTarget.
    UPROPERTY(BlueprintReadOnly)
    int32 CurrentValue = 0;

    // Effective target for this player/group configuration.
    // Recomputed when group size changes. Stored so the client can display
    // progress without loading the definition asset.
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
struct GAMECORE_API FQuestRuntime : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag QuestId;

    // Current stage. Matches a state tag in UQuestDefinition::StageGraph.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag CurrentStageTag;

    // Progress counters for the current stage.
    // Only entries with bReEvaluateOnly=false are present here.
    UPROPERTY(BlueprintReadOnly)
    TArray<FQuestTrackerEntry> Trackers;

    // Unix timestamp (seconds) of last completion.
    // Used by URequirement_QuestCooldown for elapsed-time and cadence checks.
    // 0 = never completed.
    UPROPERTY(BlueprintReadOnly)
    int64 LastCompletedTimestamp = 0;

    // ── FFastArraySerializer callbacks ──────────────────────────────────────
    void PostReplicatedAdd(const struct FQuestRuntimeArray& Array);
    void PostReplicatedChange(const struct FQuestRuntimeArray& Array);
    void PreReplicatedRemove(const struct FQuestRuntimeArray& Array);

    FQuestTrackerEntry* FindTracker(const FGameplayTag& Key)
    {
        return Trackers.FindByPredicate(
            [&](const FQuestTrackerEntry& E){ return E.TrackerKey == Key; });
    }
    const FQuestTrackerEntry* FindTracker(const FGameplayTag& Key) const
    {
        return Trackers.FindByPredicate(
            [&](const FQuestTrackerEntry& E){ return E.TrackerKey == Key; });
    }
};
```

> **Note on `EQuestMemberRole`:** Member role (Primary/Helper) is specific to the shared quest extension and lives only on the shared quest runtime, not in the base `FQuestRuntime`. See `USharedQuestCoordinator` for the shared extension's runtime data.

---

## `FQuestRuntimeArray`

```cpp
USTRUCT()
struct GAMECORE_API FQuestRuntimeArray : public FFastArraySerializer
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
| `CompletedQuestTags` | Yes — `ReplicatedUsing` | Full tag container, infrequent changes |
| `FQuestTrackerEntry::CurrentValue` | Yes | Client uses for progress bars |
| `FQuestTrackerEntry::EffectiveTarget` | Yes | Needed client-side for progress display |
| `LastCompletedTimestamp` | Yes | Client computes cooldown countdown from this |

> **Owning client only.** Quest runtime data is replicated only to the player who owns it.

---

## Building `FRequirementContext` from `FQuestRuntime`

```cpp
FRequirementContext UQuestComponent::BuildRequirementContext(
    const FQuestRuntime& QuestRuntime,
    const UQuestDefinition* Definition) const
{
    FRequirementContext Ctx;
    Ctx.PlayerState   = GetOwner<APlayerState>();
    Ctx.World         = GetWorld();
    Ctx.Instigator    = Ctx.PlayerState ? Ctx.PlayerState->GetPawn() : nullptr;
    Ctx.QuestComponent = const_cast<UQuestComponent*>(this); // cached for requirements

    FRequirementPayload Payload;
    for (const FQuestTrackerEntry& Tracker : QuestRuntime.Trackers)
        Payload.Counters.Add(Tracker.TrackerKey, Tracker.CurrentValue);

    Ctx.PersistedData.Add(QuestRuntime.QuestId, MoveTemp(Payload));
    return Ctx;
}
```

> `QuestComponent` is set directly so requirements like `URequirement_QuestCompleted` and `URequirement_ActiveQuestCount` can access quest state without `FindComponentByClass`. `LastCompletedTimestamp` is no longer injected into the payload — `URequirement_QuestCooldown` reads it directly from `Ctx.QuestComponent->FindActiveQuest(PayloadKey)`.

---

## Persistence: `Serialize_Save` and `Serialize_Load`

**Schema version: 1**

Both methods live on `UQuestComponent`. Tags are serialized as `FName` strings for forward compatibility across tag list changes.

```cpp
void UQuestComponent::Serialize_Save(FArchive& Ar)
{
    // CompletedQuestTags
    TArray<FName> TagNames;
    for (const FGameplayTag& T : CompletedQuestTags)
        TagNames.Add(T.GetTagName());
    Ar << TagNames;

    // ActiveQuests
    int32 Count = ActiveQuests.Items.Num();
    Ar << Count;
    for (const FQuestRuntime& Q : ActiveQuests.Items)
    {
        FName QuestIdName   = Q.QuestId.GetTagName();
        FName StageTagName  = Q.CurrentStageTag.GetTagName();
        Ar << QuestIdName << StageTagName;
        Ar << Q.LastCompletedTimestamp;

        int32 TrackerCount = Q.Trackers.Num();
        Ar << TrackerCount;
        for (const FQuestTrackerEntry& T : Q.Trackers)
        {
            FName TrackerKeyName = T.TrackerKey.GetTagName();
            Ar << TrackerKeyName << T.CurrentValue << T.EffectiveTarget;
        }
    }
}

void UQuestComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    // CompletedQuestTags
    TArray<FName> TagNames;
    Ar << TagNames;
    CompletedQuestTags.Reset();
    for (const FName& Name : TagNames)
    {
        FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Name, false);
        if (Tag.IsValid())
            CompletedQuestTags.AddTag(Tag);
        else
            UE_LOG(LogQuest, Warning,
                TEXT("Serialize_Load: Unknown CompletedQuestTag '%s' — skipped."),
                *Name.ToString());
    }

    // ActiveQuests
    int32 Count = 0;
    Ar << Count;
    ActiveQuests.Items.Reset();
    for (int32 i = 0; i < Count; ++i)
    {
        FName QuestIdName, StageTagName;
        Ar << QuestIdName << StageTagName;

        FGameplayTag QuestId  = FGameplayTag::RequestGameplayTag(QuestIdName,  false);
        FGameplayTag StageTag = FGameplayTag::RequestGameplayTag(StageTagName, false);

        int64 LastCompleted = 0;
        Ar << LastCompleted;

        int32 TrackerCount = 0;
        Ar << TrackerCount;

        TArray<FQuestTrackerEntry> Trackers;
        for (int32 j = 0; j < TrackerCount; ++j)
        {
            FName TrackerKeyName;
            int32 CurrentValue = 0, EffectiveTarget = 1;
            Ar << TrackerKeyName << CurrentValue << EffectiveTarget;

            FGameplayTag TrackerKey =
                FGameplayTag::RequestGameplayTag(TrackerKeyName, false);
            if (TrackerKey.IsValid())
            {
                FQuestTrackerEntry Entry;
                Entry.TrackerKey     = TrackerKey;
                Entry.CurrentValue   = CurrentValue;
                Entry.EffectiveTarget = EffectiveTarget;
                Trackers.Add(Entry);
            }
            else
            {
                UE_LOG(LogQuest, Warning,
                    TEXT("Serialize_Load: Unknown TrackerKey '%s' in quest '%s' — skipped."),
                    *TrackerKeyName.ToString(), *QuestIdName.ToString());
            }
        }

        // Skip entire quest runtime if either core tag is unknown.
        // This prevents a broken FQuestRuntime from entering ActiveQuests.
        if (!QuestId.IsValid() || !StageTag.IsValid())
        {
            UE_LOG(LogQuest, Warning,
                TEXT("Serialize_Load: Quest '%s' or stage '%s' tag unknown — skipped."),
                *QuestIdName.ToString(), *StageTagName.ToString());
            continue;
        }

        FQuestRuntime Runtime;
        Runtime.QuestId               = QuestId;
        Runtime.CurrentStageTag       = StageTag;
        Runtime.LastCompletedTimestamp = LastCompleted;
        Runtime.Trackers              = MoveTemp(Trackers);
        ActiveQuests.Items.Add(MoveTemp(Runtime));
    }

    ActiveQuests.MarkArrayDirty();
}

void UQuestComponent::Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion)
{
    // v1 → future: add migration steps here as the serialization layout evolves.
    // Additive-only changes (new fields with defaults) do not require migration.
    // Structural changes (removed fields, renamed trackers) must be handled here.
    // Log all migrations with UE_LOG(LogQuest, ...) for live-ops diagnostics.
}
```

---

## Post-Load: Validate and Register

After `Serialize_Load` completes, `UQuestComponent::BeginPlay` (server path) calls `ValidateActiveQuestsOnLogin` followed by watcher registration. See `UQuestComponent.md` for the full ordered flow.
