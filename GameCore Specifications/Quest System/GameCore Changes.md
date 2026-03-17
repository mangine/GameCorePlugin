# GameCore Changes

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**Module:** `GameCore` (plugin — changes apply globally to all systems)

The Quest System requires two targeted additions to the GameCore Requirement System. These are generic improvements — they have no knowledge of quests.

---

## 1. `FRequirementPayload` (new USTRUCT)

**File:** `GameCore/Source/GameCore/Requirements/RequirementPayload.h`

```cpp
// A keyed data bag injected into FRequirementContext at evaluation time.
// Carries persisted runtime state (counters, floats) that stateless URequirement
// subclasses need to read without coupling to any storage system.
//
// The payload is constructed by the owning system (e.g. UQuestComponent) and
// placed into FRequirementContext::PersistedData before calling Evaluate().
// Requirements never write to this struct — it is read-only at evaluation time.
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementPayload
{
    GENERATED_BODY()

    // Integer counters: kill counts, collection counts, interaction counts, etc.
    UPROPERTY(BlueprintReadOnly)
    TMap<FGameplayTag, int32> Counters;

    // Float values: time elapsed, distance travelled, percentages, etc.
    UPROPERTY(BlueprintReadOnly)
    TMap<FGameplayTag, float> Floats;

    // Helpers
    bool GetCounter(const FGameplayTag& Key, int32& OutValue) const
    {
        if (const int32* Found = Counters.Find(Key))
        {
            OutValue = *Found;
            return true;
        }
        return false;
    }

    bool GetFloat(const FGameplayTag& Key, float& OutValue) const
    {
        if (const float* Found = Floats.Find(Key))
        {
            OutValue = *Found;
            return true;
        }
        return false;
    }
};
```

---

## 2. `FRequirementContext` — Addition

**File:** `GameCore/Source/GameCore/Requirements/RequirementContext.h` (existing)

Add one field to the existing struct:

```cpp
// Injected persisted data keyed by payload tag.
// Populated by the owning system before passing context to Evaluate().
// Each entry is a self-contained payload for one logical data domain
// (e.g. Quest.Tracker.KillCount -> FRequirementPayload with Counters).
//
// Requirements that read from this map must declare GetDataAuthority() == Both,
// since the owning system controls what data is available on each side.
//
// Example injection (in UQuestComponent::BuildRequirementContext):
//   FRequirementPayload Payload;
//   Payload.Counters.Add(TAG_Quest_Tracker_KillCount, 2);
//   Context.PersistedData.Add(TAG_Quest_Payload_Stage1, Payload);
UPROPERTY()
TMap<FGameplayTag, FRequirementPayload> PersistedData;
```

> **Design rule:** The key is a domain tag, not a tracker tag. One payload entry per logical domain (e.g. one per quest stage). Individual counter/float tags live inside the payload. This keeps the map flat and lookup O(1) at both levels.

---

## 3. `URequirement_Persisted` (new abstract class)

**File:** `GameCore/Source/GameCore/Requirements/RequirementPersisted.h / .cpp`

An abstract intermediate class between `URequirement` and any requirement that needs to read from `FRequirementContext::PersistedData`. Handles key lookup, missing-payload failure, and seals `Evaluate()` so subclasses cannot accidentally bypass the lookup.

```cpp
// Abstract base for all requirements that read from FRequirementContext::PersistedData.
// Subclasses implement EvaluateWithPayload() instead of Evaluate().
// The payload lookup and missing-key failure are handled here — subclasses
// never touch FRequirementContext::PersistedData directly.
UCLASS(Abstract, EditInlineNew, CollapseCategories)
class GAMECORE_API URequirement_Persisted : public URequirement
{
    GENERATED_BODY()
public:
    // Tag used to look up this requirement's FRequirementPayload in
    // FRequirementContext::PersistedData. Set by the designer on each instance.
    // Must match the key used by the owning system when building FRequirementContext.
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Payload"))
    FGameplayTag PayloadKey;

    // Data authority is Both: the owning system decides what is available on each side.
    // The client receives replicated FQuestRuntime data, so payload can be built client-side.
    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        return ERequirementDataAuthority::Both;
    }

    // Subclasses implement this. Payload is guaranteed valid when called.
    // The base Evaluate() handles lookup and fires a descriptive Fail if the key
    // is not present in Context.PersistedData.
    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const
    {
        // Pure virtual — force subclasses to implement.
        // Declared with a body here only to satisfy PURE_VIRTUAL macro needs.
        return FRequirementResult::Fail(
            LOCTEXT("NotImplemented", "EvaluateWithPayload not implemented."));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("[Persisted] PayloadKey: %s"),
            *PayloadKey.ToString());
    }
#endif

protected:
    // Sealed. Subclasses must not override Evaluate() — use EvaluateWithPayload().
    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override final
    {
        if (!PayloadKey.IsValid())
        {
            return FRequirementResult::Fail(
                LOCTEXT("NoPayloadKey",
                    "Persisted requirement has no PayloadKey set."));
        }

        const FRequirementPayload* Payload =
            Context.PersistedData.Find(PayloadKey);

        if (!Payload)
        {
            // Not a hard failure for client-side evaluation where data may not
            // yet be present — treated as a soft miss. The owning system is
            // responsible for ensuring payload is populated before evaluation.
            return FRequirementResult::Fail(
                FText::Format(
                    LOCTEXT("MissingPayload",
                        "Payload not found for key: {0}"),
                    FText::FromString(PayloadKey.ToString())));
        }

        return EvaluateWithPayload(Context, *Payload);
    }
};
```

### Subclassing Checklist

- Do **not** override `Evaluate()` — it is sealed.
- Do **not** redeclare `Abstract` on the subclass.
- Always set `DisplayName` in `UCLASS` specifier.
- Set `PayloadKey` in the asset Details panel — it must match the key the owning system injects.
- `GetDescription()` should include `PayloadKey` and the threshold being checked.
- `GetWatchedEvents()` should declare the event tags that invalidate this requirement in the watcher system.

### Example Subclass

```cpp
// Lives in PirateGame Quest module, not GameCore.
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Kill Count Tracker"))
class PIRATEQUESTS_API URequirement_KillCount : public URequirement_Persisted
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement", meta=(ClampMin=1))
    int32 RequiredKills = 1;

    // Tag identifying which counter inside the payload holds the kill count.
    // e.g. Quest.Counter.KillCount
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Counter"))
    FGameplayTag CounterTag;

    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const override
    {
        int32 CurrentKills = 0;
        Payload.GetCounter(CounterTag, CurrentKills);

        if (CurrentKills >= RequiredKills)
            return FRequirementResult::Pass();

        return FRequirementResult::Fail(
            FText::Format(
                LOCTEXT("NeedMoreKills", "Kills: {0} / {1}"),
                FText::AsNumber(CurrentKills),
                FText::AsNumber(RequiredKills)));
    }

    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        // Invalidate when mob kill event fires.
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag(
                TEXT("RequirementEvent.Quest.TrackerUpdated")));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Kill Count >= %d (payload: %s, counter: %s)"),
            RequiredKills,
            *PayloadKey.ToString(),
            *CounterTag.ToString());
    }
#endif
};
```

---

## RequirementEvent Tags — Quest Module Additions

Add to `DefaultGameplayTags.ini` in the Quest module:

```ini
+GameplayTagList=(Tag="RequirementEvent.Quest.TrackerUpdated",  DevComment="A quest progress tracker counter changed")
+GameplayTagList=(Tag="RequirementEvent.Quest.StageChanged",   DevComment="Active quest moved to a new stage")
+GameplayTagList=(Tag="RequirementEvent.Quest.Completed",      DevComment="A quest was completed — monotonic requirements re-check")
+GameplayTagList=(Tag="Quest.Payload",                        DevComment="Namespace for payload domain keys")
+GameplayTagList=(Tag="Quest.Counter",                        DevComment="Namespace for counter keys within a payload")
```
