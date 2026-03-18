# GameCore Changes

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**Module:** `GameCore` (plugin — changes apply globally to all systems)

The Quest System requires targeted additions to the GameCore plugin. All are generic improvements with no knowledge of quests.

---

## 1. `IGroupProvider` + `UGroupProviderDelegates`

**File:** `GameCore/Source/GameCore/Interfaces/GroupProvider.h`

A generic interface for any actor or component that owns group membership data. Used by `USharedQuestComponent` to read group state without coupling to any concrete party system. Any grouping system — parties, ship crews, squads, guilds — implements this interface.

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
    // OutMembers is valid only for the current frame — do not cache the array.
    virtual void GetGroupMembers(TArray<APlayerState*>& OutMembers) const = 0;
};
```

### `UGroupProviderDelegates` — Delegate-Backed Default Implementation

**File:** `GameCore/Source/GameCore/Interfaces/GroupProvider.h` (same file)

An optional `UActorComponent` that provides a delegate-backed default implementation of `IGroupProvider`. Games that do not yet have a concrete party system, or that prefer loose wiring over subclassing, can add this component to `APlayerState` and bind the delegates from whatever system owns group data.

When a delegate is unbound, the method returns a safe solo fallback. When all three are bound, behaviour is identical to a fully implemented `IGroupProvider`.

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UGroupProviderDelegates : public UActorComponent
{
    GENERATED_BODY()
public:
    // Bind to return current group size. Unbound fallback: returns 1 (solo).
    TDelegate<int32()> GetGroupSizeDelegate;

    // Bind to return whether this player is the group leader. Unbound fallback: false.
    TDelegate<bool()> IsGroupLeaderDelegate;

    // Bind to populate OutMembers with all group members including this player.
    // Unbound fallback: OutMembers contains only the owning PlayerState.
    TDelegate<void(TArray<APlayerState*>&)> GetGroupMembersDelegate;

    // ── IGroupProvider forwarding helpers ──────────────────────────────────────────
    // Called by APlayerState::IGroupProvider implementation to forward to delegates.

    int32 ForwardGetGroupSize() const
    {
        if (GetGroupSizeDelegate.IsBound())
            return GetGroupSizeDelegate.Execute();
        return 1;
    }

    bool ForwardIsGroupLeader() const
    {
        if (IsGroupLeaderDelegate.IsBound())
            return IsGroupLeaderDelegate.Execute();
        return false;
    }

    void ForwardGetGroupMembers(TArray<APlayerState*>& OutMembers) const
    {
        if (GetGroupMembersDelegate.IsBound())
        {
            GetGroupMembersDelegate.Execute(OutMembers);
            return;
        }
        // Solo fallback: only this player.
        if (APlayerState* PS = GetOwner<APlayerState>())
            OutMembers.Add(PS);
    }
};
```

### Integration Pattern

A game that wants delegate-based wiring adds `UGroupProviderDelegates` to `APlayerState` and implements `IGroupProvider` by forwarding to it:

```cpp
// In AMyPlayerState.h:
class AMyPlayerState : public APlayerState, public IGroupProvider
{
    GENERATED_BODY()
public:
    // IGroupProvider
    virtual int32 GetGroupSize() const override
        { return GroupProviderDelegates->ForwardGetGroupSize(); }
    virtual bool IsGroupLeader() const override
        { return GroupProviderDelegates->ForwardIsGroupLeader(); }
    virtual void GetGroupMembers(TArray<APlayerState*>& Out) const override
        { GroupProviderDelegates->ForwardGetGroupMembers(Out); }

    UPROPERTY()
    TObjectPtr<UGroupProviderDelegates> GroupProviderDelegates;
};

// Elsewhere — e.g. in a party component's BeginPlay — bind the delegates:
PlayerState->GroupProviderDelegates->GetGroupSizeDelegate.BindUObject(
    this, &UMyPartyComponent::GetMemberCount);
PlayerState->GroupProviderDelegates->IsGroupLeaderDelegate.BindUObject(
    this, &UMyPartyComponent::IsThisPlayerLeader);
PlayerState->GroupProviderDelegates->GetGroupMembersDelegate.BindUObject(
    this, &UMyPartyComponent::GetAllMemberPlayerStates);
```

A game with a concrete party system can instead implement `IGroupProvider` directly on `APlayerState` without using `UGroupProviderDelegates` at all — both paths are valid.

### How `USharedQuestComponent` Accesses `IGroupProvider`

`USharedQuestComponent` lives on `APlayerState`. It reads group data by casting its own owner:

```cpp
IGroupProvider* USharedQuestComponent::GetGroupProvider() const
{
    return Cast<IGroupProvider>(GetOwner()); // APlayerState
}
```

If `APlayerState` does not implement `IGroupProvider`, the cast returns null. `USharedQuestComponent` treats null as ungrouped and falls back to solo behavior — no crash, no assertion.

> **Design rule:** `IGroupProvider` is read-only from the quest system’s perspective. The quest system never writes group state through this interface — it only reads member lists, group size, and leader status.

---

## 2. `FRequirementPayload` (new USTRUCT)

**File:** `GameCore/Source/GameCore/Requirements/RequirementPayload.h`

```cpp
// A keyed data bag injected into FRequirementContext at evaluation time.
// Carries persisted runtime state (counters, floats) that stateless URequirement
// subclasses need to read without coupling to any storage system.
//
// Constructed by the owning system (e.g. UQuestComponent) and placed into
// FRequirementContext::PersistedData before calling Evaluate().
// Requirements never write to this struct — read-only at evaluation time.
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementPayload
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TMap<FGameplayTag, int32> Counters;

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
// The key is a domain tag (e.g. QuestId), not an individual counter tag.
// Individual counters/floats live inside the FRequirementPayload.
UPROPERTY()
TMap<FGameplayTag, FRequirementPayload> PersistedData;
```

---

## 4. `URequirement_Persisted` (new abstract class)

**File:** `GameCore/Source/GameCore/Requirements/RequirementPersisted.h / .cpp`

Abstract intermediate class for requirements that read from `FRequirementContext::PersistedData`. Seals `Evaluate()` so subclasses cannot bypass the payload lookup.

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories)
class GAMECORE_API URequirement_Persisted : public URequirement
{
    GENERATED_BODY()
public:
    // Domain tag matching the key injected by the owning system's ContextBuilder.
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Payload"))
    FGameplayTag PayloadKey;

    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        return ERequirementDataAuthority::Both;
    }

    // Subclasses implement this instead of Evaluate().
    // Payload is guaranteed valid when called.
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
    // Sealed — subclasses must not override Evaluate().
    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override final
    {
        if (!PayloadKey.IsValid())
            return FRequirementResult::Fail(
                LOCTEXT("NoPayloadKey", "Persisted requirement has no PayloadKey."));

        const FRequirementPayload* Payload = Context.PersistedData.Find(PayloadKey);
        if (!Payload)
            return FRequirementResult::Fail(
                FText::Format(
                    LOCTEXT("MissingPayload", "No payload for key: {0}"),
                    FText::FromString(PayloadKey.ToString())));

        return EvaluateWithPayload(Context, *Payload);
    }
};
```

### Subclassing Checklist

- Do **not** override `Evaluate()` — it is sealed.
- Do **not** redeclare `Abstract` on the subclass.
- Always set `DisplayName` in `UCLASS`.
- `PayloadKey` must match the domain key injected by the owning system’s `ContextBuilder`.
- Override `GetWatchedEvents_Implementation` to declare invalidation tags.
- Override `GetDescription()` to include `PayloadKey` and threshold values.

### Example Subclass

```cpp
// Lives in the game module or a game-specific module — not in GameCore.
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Kill Count Tracker"))
class GAMECORE_API URequirement_KillCount : public URequirement_Persisted
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

```ini
+GameplayTagList=(Tag="RequirementEvent.Quest.TrackerUpdated", DevComment="A quest tracker counter changed")
+GameplayTagList=(Tag="RequirementEvent.Quest.StageChanged",   DevComment="Active quest moved to a new stage")
+GameplayTagList=(Tag="RequirementEvent.Quest.Completed",      DevComment="A quest was completed")
+GameplayTagList=(Tag="Quest.Payload",                        DevComment="Namespace for payload domain keys")
+GameplayTagList=(Tag="Quest.Counter",                        DevComment="Namespace for counter keys within a payload")
+GameplayTagList=(Tag="Quest.Counter.LastCompleted",          DevComment="Float: Unix timestamp of last quest completion")
```
