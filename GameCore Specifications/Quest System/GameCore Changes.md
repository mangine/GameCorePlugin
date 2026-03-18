# GameCore Changes

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**Module:** `GameCore` (plugin — changes apply globally to all systems)

The Quest System requires targeted additions to the GameCore plugin. All are generic improvements with no knowledge of quests.

---

## 1. `IGroupProvider` + `UGroupProviderDelegates`

**File:** `GameCore/Source/GameCore/Interfaces/GroupProvider.h`

A generic interface for any actor or component that owns group membership data. Used by `USharedQuestComponent` to read group state without coupling to any concrete party system.

```cpp
UINTERFACE(MinimalAPI, BlueprintType)
class UGroupProvider : public UInterface { GENERATED_BODY() };

class GAMECORE_API IGroupProvider
{
    GENERATED_BODY()
public:
    virtual int32 GetGroupSize() const = 0;
    virtual bool IsGroupLeader() const = 0;
    virtual void GetGroupMembers(TArray<APlayerState*>& OutMembers) const = 0;

    // Returns the actor that owns the USharedQuestCoordinator for this group.
    // USharedQuestComponent calls this to locate the coordinator without
    // coupling to any concrete party actor type.
    // Return nullptr if no coordinator actor exists (e.g. player is not in a group).
    virtual AActor* GetGroupActor() const = 0;
};
```

### `UGroupProviderDelegates` — Delegate-Backed Default Implementation

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UGroupProviderDelegates : public UActorComponent
{
    GENERATED_BODY()
public:
    TDelegate<int32()>                      GetGroupSizeDelegate;
    TDelegate<bool()>                       IsGroupLeaderDelegate;
    TDelegate<void(TArray<APlayerState*>&)> GetGroupMembersDelegate;
    TDelegate<AActor*()>                    GetGroupActorDelegate;

    int32 ForwardGetGroupSize() const
    {
        return GetGroupSizeDelegate.IsBound()
            ? GetGroupSizeDelegate.Execute() : 1;
    }
    bool ForwardIsGroupLeader() const
    {
        return IsGroupLeaderDelegate.IsBound()
            ? IsGroupLeaderDelegate.Execute() : false;
    }
    void ForwardGetGroupMembers(TArray<APlayerState*>& Out) const
    {
        if (GetGroupMembersDelegate.IsBound())
            { GetGroupMembersDelegate.Execute(Out); return; }
        if (APlayerState* PS = GetOwner<APlayerState>()) Out.Add(PS);
    }
    AActor* ForwardGetGroupActor() const
    {
        return GetGroupActorDelegate.IsBound()
            ? GetGroupActorDelegate.Execute() : nullptr;
    }
};
```

### Integration Pattern

```cpp
class AMyPlayerState : public APlayerState, public IGroupProvider
{
    virtual int32 GetGroupSize() const override
        { return GroupProviderDelegates->ForwardGetGroupSize(); }
    virtual bool IsGroupLeader() const override
        { return GroupProviderDelegates->ForwardIsGroupLeader(); }
    virtual void GetGroupMembers(TArray<APlayerState*>& Out) const override
        { GroupProviderDelegates->ForwardGetGroupMembers(Out); }
    virtual AActor* GetGroupActor() const override
        { return GroupProviderDelegates->ForwardGetGroupActor(); }

    UPROPERTY()
    TObjectPtr<UGroupProviderDelegates> GroupProviderDelegates;
};
```

> **Design rule:** `IGroupProvider` is read-only from the quest system’s perspective.

---

## 2. `FRequirementPayload` (new USTRUCT)

**File:** `GameCore/Source/GameCore/Requirements/RequirementPayload.h`

```cpp
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
            { OutValue = *Found; return true; }
        return false;
    }
    bool GetFloat(const FGameplayTag& Key, float& OutValue) const
    {
        if (const float* Found = Floats.Find(Key))
            { OutValue = *Found; return true; }
        return false;
    }
};
```

---

## 3. `FRequirementContext` — Additions

**File:** `GameCore/Source/GameCore/Requirements/RequirementContext.h` (existing)

Two fields are added:

```cpp
// Injected persisted data keyed by payload domain tag (e.g. QuestId).
// Populated by the owning system's ContextBuilder before evaluation.
UPROPERTY()
TMap<FGameplayTag, FRequirementPayload> PersistedData;

// Cached pointer to the owning player's UQuestComponent.
// Set by UQuestComponent::BuildRequirementContext.
// Allows quest requirements (URequirement_QuestCompleted, URequirement_ActiveQuestCount)
// to read quest state directly without calling FindComponentByClass on every evaluation.
// Null when the context is not quest-related — requirements must null-check before use.
UPROPERTY()
TObjectPtr<UQuestComponent> QuestComponent = nullptr;
```

> **Performance note:** `FindComponentByClass` on a hot evaluation path is O(N components). Caching the pointer in `FRequirementContext` reduces all quest requirement evaluations to a single pointer dereference.

---

## 4. `URequirement_Persisted` (new abstract class)

**File:** `GameCore/Source/GameCore/Requirements/RequirementPersisted.h / .cpp`

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories)
class GAMECORE_API URequirement_Persisted : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Requirement",
              meta=(Categories="Quest.Payload"))
    FGameplayTag PayloadKey;

    virtual ERequirementDataAuthority GetDataAuthority() const override
    { return ERequirementDataAuthority::Both; }

    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const
    {
        return FRequirementResult::Fail(
            LOCTEXT("NotImplemented", "EvaluateWithPayload not implemented."));
    }

protected:
    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override final
    {
        if (!PayloadKey.IsValid())
            return FRequirementResult::Fail(
                LOCTEXT("NoPayloadKey", "Persisted requirement has no PayloadKey."));
        const FRequirementPayload* Payload = Context.PersistedData.Find(PayloadKey);
        if (!Payload)
            return FRequirementResult::Fail(
                FText::Format(LOCTEXT("MissingPayload", "No payload for key: {0}"),
                    FText::FromString(PayloadKey.ToString())));
        return EvaluateWithPayload(Context, *Payload);
    }
};
```

---

## 5. `UQuestTransitionRule` + `UQuestStateNode`

**File:** `GameCore/Source/GameCore/Quest/QuestTransitionRule.h / .cpp`

These are the quest-system’s extension points into the State Machine system. They allow designers to author stage transition conditions and terminal state flags using the same requirement system used everywhere else.

### `UQuestStateNode`

```cpp
// Quest-specific state node. Adds terminal flags read by UQuestComponent
// when the state machine enters this state.
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Stage"))
class GAMECORE_API UQuestStateNode : public UStateNodeBase
{
    GENERATED_BODY()
public:
    // Entering this state triggers Internal_CompleteQuest in UQuestComponent.
    UPROPERTY(EditDefaultsOnly, Category="Quest Stage")
    bool bIsCompletionState = false;

    // Entering this state triggers Internal_FailQuest in UQuestComponent.
    UPROPERTY(EditDefaultsOnly, Category="Quest Stage")
    bool bIsFailureState = false;

    // Override base: no-op. Quest side effects are driven by UQuestComponent
    // reading bIsCompletionState / bIsFailureState after FindFirstPassingTransition.
    virtual void OnEnter(UStateMachineComponent* Component) override {}
    virtual void OnExit(UStateMachineComponent* Component)  override {}
};
```

### `UQuestTransitionRule`

```cpp
// Quest-specific transition rule. Evaluates a URequirementList against
// the FRequirementContext injected by UQuestComponent as the ContextObject.
// Allows full branching stage graphs with requirement-driven transitions.
UCLASS(EditInlineNew, CollapseCategories,
       meta=(DisplayName="Quest Requirement Transition"))
class GAMECORE_API UQuestTransitionRule : public UTransitionRule
{
    GENERATED_BODY()
public:
    // Requirements that must all pass for this transition to fire.
    // Evaluated against the FRequirementContext provided by UQuestComponent.
    // Authority is determined by UQuestDefinition::CheckAuthority at the call site.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Transition")
    TObjectPtr<URequirementList> Requirements;

    virtual bool Evaluate(
        UStateMachineComponent* Component,
        UObject* ContextObject) const override
    {
        if (!Requirements) return true; // No requirements = unconditional

        // ContextObject is an FRequirementContext* passed by UQuestComponent.
        // UStateMachineAsset::FindFirstPassingTransition passes it through.
        const FRequirementContext* Ctx =
            static_cast<FRequirementContext*>(ContextObject);
        if (!Ctx) return false;

        return Requirements->Evaluate(*Ctx).bPassed;
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return Requirements
            ? FString::Printf(TEXT("Requirements: %s"), *Requirements->GetName())
            : TEXT("No requirements (unconditional)");
    }
#endif
};
```

### `UStateMachineAsset::FindFirstPassingTransition` (addition to State Machine spec)

This pure utility method is added to `UStateMachineAsset` so any system can evaluate transitions against an asset without needing `UStateMachineComponent`:

```cpp
// Returns the ToState tag of the first transition from FromState whose rule
// passes against ContextObject, or an invalid tag if none pass.
// Pure — no side effects. Called by UQuestComponent to resolve stage advances.
FGameplayTag UStateMachineAsset::FindFirstPassingTransition(
    const FGameplayTag& FromStateTag,
    UObject* ContextObject) const
{
    // Check AnyState transitions first (highest priority).
    for (const FStateTransition& T : AnyStateTransitions)
    {
        if (T.Rule && T.Rule->Evaluate(nullptr, ContextObject))
            return T.ToState;
    }
    // Then check transitions from the specific state.
    for (const FStateTransition& T : Transitions)
    {
        if (T.FromState == FromStateTag
            && T.Rule
            && T.Rule->Evaluate(nullptr, ContextObject))
            return T.ToState;
    }
    return FGameplayTag(); // invalid = no passing transition found
}
```

> **Note:** `UStateMachineComponent*` is passed as `nullptr` here because `UQuestComponent` drives quest state directly — no `UStateMachineComponent` is present on `APlayerState`. `UQuestTransitionRule::Evaluate` only uses the `ContextObject` parameter, never the component pointer. If a transition rule requires the component, it is not suitable for quest use.

---

## RequirementEvent Tags — Quest Module Additions

```ini
+GameplayTagList=(Tag="RequirementEvent.Quest.TrackerUpdated")
+GameplayTagList=(Tag="RequirementEvent.Quest.StageChanged")
+GameplayTagList=(Tag="RequirementEvent.Quest.Completed")
+GameplayTagList=(Tag="Quest.Payload")
+GameplayTagList=(Tag="Quest.Counter")
+GameplayTagList=(Tag="Quest.Counter.LastCompleted")
```
