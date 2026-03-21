# Quest Runtime Structs

**File:** `Quest/Runtime/QuestRuntime.h`

Per-player mutable quest state. Replicated, serialized, and used to build `FRequirementContext` at evaluation time. Never stored in `UQuestDefinition`.

---

## `FQuestEvaluationContext`

**File:** `Quest/Runtime/QuestRuntime.h`

Snapshot struct injected into `FRequirementContext::Data` before requirement evaluation. Quest-owned requirements (`URequirement_QuestCompleted`, `URequirement_QuestCooldown`, `URequirement_ActiveQuestCount`) cast `Context.Data` to `FQuestEvaluationContext*` and retrieve `UQuestComponent` from `PlayerState`.

> **Why not cache `UQuestComponent*` directly in the context?** Would create a compile-time import from the zero-dependency `Requirements/` module into the Quest module. `FindComponentByClass` on `APlayerState` is called at most a few times per evaluation flush — not per frame. The overhead is negligible.

```cpp
// File: Quest/Runtime/QuestRuntime.h
USTRUCT()
struct YOURGAME_API FQuestEvaluationContext
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<APlayerState> PlayerState = nullptr;

    UPROPERTY()
    TObjectPtr<UWorld> World = nullptr;
};
```

**Usage in `UQuestComponent::BuildRequirementContext`:**

```cpp
FRequirementContext UQuestComponent::BuildRequirementContext() const
{
    FQuestEvaluationContext CtxData;
    CtxData.PlayerState = GetOwner<APlayerState>();
    CtxData.World       = GetWorld();
    return FRequirementContext::Make(CtxData);
}
```

**Usage in a quest requirement's `Evaluate`:**

```cpp
FRequirementResult URequirement_QuestCompleted::Evaluate(
    const FRequirementContext& Context) const
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
        : FRequirementResult::Fail(LOCTEXT("NotDone", "Required quest not completed."));
}
```

---

## `FQuestTrackerEntry`

```cpp
USTRUCT(BlueprintType)
struct YOURGAME_API FQuestTrackerEntry
{
    GENERATED_BODY()

    // Matches FQuestProgressTrackerDef::TrackerKey in the stage definition.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag TrackerKey;

    // Current accumulated value. Always >= 0, clamped to EffectiveTarget.
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

Per-player instance of one active quest. Element of `UQuestComponent::ActiveQuests` which uses `FFastArraySerializer` for efficient delta replication.

```cpp
USTRUCT(BlueprintType)
struct YOURGAME_API FQuestRuntime : public FFastArraySerializerItem
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
    // Used by URequirement_QuestCooldown.
    // 0 = never completed.
    UPROPERTY(BlueprintReadOnly)
    int64 LastCompletedTimestamp = 0;

    // FFastArraySerializer callbacks — called on the receiving (client) side.
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

> **`PostReplicatedAdd`** should trigger `RegisterClientValidatedCompletionWatchers` for the newly added quest if it is `ClientValidated`. This fixes KI-3 from the Architecture document — completion watchers must be registered for quests accepted after initial `BeginPlay`.

---

## `FQuestRuntimeArray`

```cpp
USTRUCT()
struct YOURGAME_API FQuestRuntimeArray : public FFastArraySerializer
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
| `ActiveQuests` | Yes — `FFastArraySerializer` | Delta-replicated per-item to owning client only |
| `CompletedQuestTags` | Yes — `ReplicatedUsing` | Full container; infrequent changes |
| `FQuestTrackerEntry::CurrentValue` | Yes | Client uses for progress bars |
| `FQuestTrackerEntry::EffectiveTarget` | Yes | Needed client-side for progress display without loading definition |
| `LastCompletedTimestamp` | Yes | Client computes cooldown countdown from this |

> **Owning client only.** Quest runtime data is replicated only to the player who owns the `APlayerState`.

---

## Persistence: `Serialize_Save` and `Serialize_Load`

**Schema version: 1**

Both methods live on `UQuestComponent` and implement `IPersistableComponent`. Tags are serialized as `FName` strings for forward compatibility across tag list changes. Unknown tags are skipped with a warning — no crash on content updates.

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
        FName QuestIdName  = Q.QuestId.GetTagName();
        FName StageTagName = Q.CurrentStageTag.GetTagName();
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

        FGameplayTag QuestId  = FGameplayTag::RequestGameplayTag(QuestIdName, false);
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
                Entry.TrackerKey      = TrackerKey;
                Entry.CurrentValue    = CurrentValue;
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

        if (!QuestId.IsValid() || !StageTag.IsValid())
        {
            UE_LOG(LogQuest, Warning,
                TEXT("Serialize_Load: Quest '%s' or stage '%s' tag unknown — skipped."),
                *QuestIdName.ToString(), *StageTagName.ToString());
            continue;
        }

        FQuestRuntime Runtime;
        Runtime.QuestId                = QuestId;
        Runtime.CurrentStageTag        = StageTag;
        Runtime.LastCompletedTimestamp = LastCompleted;
        Runtime.Trackers               = MoveTemp(Trackers);
        ActiveQuests.Items.Add(MoveTemp(Runtime));
    }

    ActiveQuests.MarkArrayDirty();
}

void UQuestComponent::Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion)
{
    // v1 → future: add migration steps here.
    // Additive-only changes (new fields with defaults) do not require migration.
    // Structural changes (removed fields, renamed trackers) must be handled here.
    // Log all migrations via UE_LOG(LogQuest, ...) for live-ops diagnostics.
}
```
