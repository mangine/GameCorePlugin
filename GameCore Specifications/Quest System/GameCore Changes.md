# GameCore Changes

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**Module:** `GameCore` (plugin — changes apply globally to all systems)

The Quest System requires three targeted additions to the GameCore plugin. All are generic improvements with no knowledge of quests.

---

## 1. `IGroupProvider` (new UInterface)

**File:** `GameCore/Source/GameCore/Interfaces/GroupProvider.h`

A generic interface for any actor or component that owns group/party membership data. Used by `USharedQuestComponent` to read group state without coupling to any concrete party system. Any grouping system — parties, ship crews, squads, guilds — implements this interface.

```cpp
UINTERFACE(MinimalAPI, BlueprintType)
class UGroupProvider : public UInterface { GENERATED_BODY() };

class GAMECORE_API IGroupProvider
{
    GENERATED_BODY()
public:
    // Number of members currently in the group, including this player.
    // Returns 1 if the player is ungrouped.
    virtual int32 GetGroupSize() const = 0;

    // True if this PlayerState is currently the group leader.
    virtual bool IsGroupLeader() const = 0;

    // All PlayerState members of the group, including this player.
    // Returned array is valid only for the current frame — do not cache.
    virtual TArray<APlayerState*> GetGroupMembers() const = 0;
};
```

### Where It Is Implemented

`IGroupProvider` is implemented on `APlayerState` (or on a component that `APlayerState` delegates to). The party system, when it exists, makes `APlayerState` satisfy this interface by implementing the three methods and delegating to whatever party actor owns the authoritative data.

```cpp
// Future party system example:
class AMyPlayerState : public APlayerState, public IGroupProvider
{
    virtual int32 GetGroupSize() const override
    {
        return PartyComponent ? PartyComponent->GetMemberCount() : 1;
    }
    virtual bool IsGroupLeader() const override
    {
        return PartyComponent && PartyComponent->IsLeader(this);
    }
    virtual TArray<APlayerState*> GetGroupMembers() const override
    {
        return PartyComponent ? PartyComponent->GetAllMembers()
                              : TArray<APlayerState*>{const_cast<AMyPlayerState*>(this)};
    }
private:
    UPROPERTY()
    TObjectPtr<UPartyComponent> PartyComponent;
};
```

### How `USharedQuestComponent` Accesses It

`USharedQuestComponent` lives on `APlayerState`. It reads group data by casting its own owner:

```cpp
IGroupProvider* USharedQuestComponent::GetGroupProvider() const
{
    return Cast<IGroupProvider>(GetOwner()); // APlayerState
}
```

If `APlayerState` does not implement `IGroupProvider`, the cast returns null. `USharedQuestComponent` treats null as "ungrouped" and falls back to solo behavior transparently — no crash, no error, no assertion.

```cpp
void USharedQuestComponent::OnMobKilledByGroupMember(
    const FMobKilledEventPayload& Payload)
{
    IGroupProvider* Provider = GetGroupProvider();
    if (!Provider) return; // Not in a group — nothing to fan out

    // Fan out tracker increment to all group members who have this quest active.
    for (APlayerState* Member : Provider->GetGroupMembers())
    {
        if (Member == GetOwner()) continue; // Self handled by base component
        if (UQuestComponent* QC = Member->FindComponentByClass<UQuestComponent>())
        {
            // Increment tracker for any quest the member has active
            // that matches the killed mob's tracker type.
            // The integration layer (game module) drives which tracker key to use.
            QC->Server_IncrementTracker(
                Payload.QuestId, Payload.TrackerKey, 1);
        }
    }
}
```

> **Design rule:** `IGroupProvider` is read-only from the quest system's perspective. The quest system never writes group state through this interface — it only reads member lists and leader status.

---

## 2. `FRequirementPayload` (new USTRUCT)

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

    // Float values: time elapsed, distance travelled, completion timestamps, etc.
    UPROPERTY(BlueprintReadOnly)
    TMap<FGameplayTag, float> Floats;

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

## 3. `FRequirementContext` — Addition

**File:** `GameCore/Source/GameCore/Requirements/RequirementContext.h` (existing)

Add one field:

```cpp
// Injected persisted data keyed by payload domain tag.
// Populated by the owning system's ContextBuilder before evaluation.
// Requirements reading this map must declare GetDataAuthority() == Both,
// since the owning system controls what data is available on each side.
UPROPERTY()
TMap<FGameplayTag, FRequirementPayload> PersistedData;
```

> **Design rule:** The key is a domain tag (e.g. the QuestId), not an individual counter tag. One payload entry per logical domain. Individual counters/floats live inside the payload. This keeps the top-level map small and lookup O(1) at both levels.

---

## 4. `URequirement_Persisted` (new abstract class)

**File:** `GameCore/Source/GameCore/Requirements/RequirementPersisted.h / .cpp`

An abstract intermediate class for requirements that read from `FRequirementContext::PersistedData`. Seals `Evaluate()` so subclasses cannot accidentally bypass the payload lookup.

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories)
class GAMECORE_API URequirement_Persisted : public URequirement
{
    GENERATED_BODY()
public:
    // Domain tag used to look up FRequirementPayload in FRequirementContext::PersistedData.
    // Must match the key the owning system injects via its ContextBuilder.
    // Set by the designer on each requirement instance.
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Payload"))
    FGameplayTag PayloadKey;

    // Data authority is Both: the payload is built from replicated data,
    // so it is available on both server and owning client.
    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        return ERequirementDataAuthority::Both;
    }

    // Subclasses implement this instead of Evaluate().
    // Payload is guaranteed non-null when called.
    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const
    {
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
    // Sealed. Subclasses must not override Evaluate().
    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override final
    {
        if (!PayloadKey.IsValid())
            return FRequirementResult::Fail(
                LOCTEXT("NoPayloadKey",
                    "Persisted requirement has no PayloadKey set."));

        const FRequirementPayload* Payload =
            Context.PersistedData.Find(PayloadKey);

        if (!Payload)
            return FRequirementResult::Fail(
                FText::Format(
                    LOCTEXT("MissingPayload",
                        "No payload found for key: {0}"),
                    FText::FromString(PayloadKey.ToString())));

        return EvaluateWithPayload(Context, *Payload);
    }
};
```

### Subclassing Checklist

- Do **not** override `Evaluate()` — it is sealed.
- Do **not** redeclare `Abstract` on the subclass.
- Always set `DisplayName` in `UCLASS`.
- `PayloadKey` must match the domain key injected by the owning system's `ContextBuilder`.
- Override `GetWatchedEvents_Implementation` to declare which `RequirementEvent.*` tags invalidate this requirement in the watcher.
- Override `GetDescription()` to include `PayloadKey` and the threshold being checked.

### Example Subclass (lives in game module, not GameCore)

```cpp
// PirateGame Quest module — URequirement_KillCount.h
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Kill Count Tracker"))
class PIRATEQUESTS_API URequirement_KillCount : public URequirement_Persisted
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement", meta=(ClampMin=1))
    int32 RequiredKills = 1;

    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Counter"))
    FGameplayTag CounterTag;

    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const override
    {
        int32 Current = 0;
        Payload.GetCounter(CounterTag, Current);
        if (Current >= RequiredKills) return FRequirementResult::Pass();
        return FRequirementResult::Fail(
            FText::Format(
                LOCTEXT("NeedKills", "{0} / {1} kills"),
                FText::AsNumber(Current), FText::AsNumber(RequiredKills)));
    }

    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag(
                TEXT("RequirementEvent.Quest.TrackerUpdated")));
    }
};
```

---

## RequirementEvent Tags — Quest Module Additions

Add to `DefaultGameplayTags.ini` in the Quest module:

```ini
+GameplayTagList=(Tag="RequirementEvent.Quest.TrackerUpdated", DevComment="A quest tracker counter changed")
+GameplayTagList=(Tag="RequirementEvent.Quest.StageChanged",   DevComment="Active quest moved to a new stage")
+GameplayTagList=(Tag="RequirementEvent.Quest.Completed",      DevComment="A quest was completed")
+GameplayTagList=(Tag="Quest.Payload",                        DevComment="Namespace for payload domain keys")
+GameplayTagList=(Tag="Quest.Counter",                        DevComment="Namespace for counter keys within a payload")
+GameplayTagList=(Tag="Quest.Counter.LastCompleted",          DevComment="Float: Unix timestamp of last quest completion")
```
