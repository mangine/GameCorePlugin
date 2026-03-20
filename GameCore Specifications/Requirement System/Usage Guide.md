# Usage Guide

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

Practical guide for integrating the Requirement System. Covers all three usage patterns with full code examples.

---

# Pattern 1 — Imperative One-Shot Check

Use when a system needs to validate a gate at a specific moment: confirming an RPC, checking interaction eligibility, validating an action.

**When:** Interaction confirmation, ability activation, crafting, server RPC handlers.

## Flow

```
Caller builds FRequirementContext from current world state
  → List->Evaluate(Ctx)
  → FRequirementResult { bPassed, FailureReason }
  → If Fail: send FailureReason to client via RPC
  → If Pass: execute the action
```

## Example — Server RPC confirming a mine action

```cpp
// Define a snapshot struct in the Leveling/Inventory module:
USTRUCT()
struct FMineActionContext
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() TObjectPtr<UWorld>       World       = nullptr;
};

// In the server RPC handler:
void UOreNodeComponent::Server_RequestMine_Implementation(APlayerController* Instigator)
{
    FMineActionContext CtxData;
    CtxData.PlayerState = Instigator->GetPlayerState<APlayerState>();
    CtxData.World       = GetWorld();

    FRequirementContext Ctx = FRequirementContext::Make(CtxData);

    FRequirementResult Result = NodeDefinition->MineRequirements->Evaluate(Ctx);
    if (!Result.bPassed)
    {
        Instigator->ClientRPC_ShowRequirementFailure(Result.FailureReason);
        return;
    }
    ExecuteMine(CtxData.PlayerState);
}
```

**Rules:**
- Always build context on the server from the RPC connection. Never use client-provided actor references as the subject.
- Run `URequirementLibrary::ValidateRequirements` at `BeginPlay` on any list your system holds, to catch authoring errors early.

---

# Pattern 2 — Reactive Watched Evaluation

Use when a system needs to know when requirements become met or unmet over time, without polling.

**When:** Quest availability, ability unlock visibility, UI state, NPC dialogue gate.

## Flow

```
Setup:
  Bind List->OnResultChanged
  Call List->Register(World)         ← subscribes to watcher manager
  [initial evaluation runs immediately inside Register]

State changes (player levels up, gains item, completes quest, ...):
  Owning module fires event on UGameCoreEventBus with RequirementEvent.* tag
  URequirementWatcherManager receives it, coalesces per frame
  End of frame: List->NotifyEvent(Tag, Payload) called
  List->EvaluateAllFromEvent(Context)
  If pass/fail changed → OnResultChanged.Broadcast(bPassed)
  Bound callback fires

Teardown:
  List->Unregister(World)
  List->OnResultChanged.RemoveAll(this)
```

## Example — Quest availability tracking

```cpp
// UQuestComponent registering availability requirements for a quest:
void UQuestComponent::StartTrackingAvailability(URequirementList* AvailabilityList)
{
    check(AvailabilityList);

    // Bind before Register so the initial evaluation fires our callback.
    AvailabilityList->OnResultChanged.AddUObject(
        this, &UQuestComponent::OnAvailabilityChanged);

    AvailabilityList->Register(GetWorld());
    ActiveAvailabilityList = AvailabilityList;
}

void UQuestComponent::OnAvailabilityChanged(bool bPassed)
{
    if (bPassed)
        BroadcastQuestAvailable(QuestDefinition);
    else
        BroadcastQuestUnavailable(QuestDefinition);
}

void UQuestComponent::StopTrackingAvailability()
{
    if (ActiveAvailabilityList)
    {
        ActiveAvailabilityList->Unregister(GetWorld());
        ActiveAvailabilityList->OnResultChanged.RemoveAll(this);
        ActiveAvailabilityList = nullptr;
    }
}
```

## Example — Firing the event from the leveling system

```cpp
void ULevelingComponent::OnLevelUp(APlayerState* PS, int32 OldLevel, int32 NewLevel)
{
    // Apply level-up effects ...

    FLevelChangedEvent Payload;
    Payload.PlayerState = PS;
    Payload.OldLevel    = OldLevel;
    Payload.NewLevel    = NewLevel;

    UGameCoreEventBus* Bus = GetWorld()->GetSubsystem<UGameCoreEventBus>();
    if (Bus)
    {
        Bus->Broadcast(
            FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"),
            FInstancedStruct::Make(Payload));
    }
    // No direct call to URequirementWatcherManager needed.
}
```

**Rules:**
- Always bind `OnResultChanged` before `Register()` so the initial evaluation fires the callback.
- Always call `Unregister()` when the system no longer needs to track — stale weak pointers are cleaned up lazily but explicit unregister is cleaner.
- `Unregister` does not remove delegate bindings. Call `OnResultChanged.RemoveAll(this)` separately.

---

# Pattern 3 — Mixed Lists (Imperative + Event requirements)

A single `URequirementList` may contain requirements that support both evaluation paths. Requirements that are imperative-only return their result from `Evaluate`. Requirements that are event-only return `Fail` from `Evaluate` and evaluate correctly from `EvaluateFromEvent`.

**Design rule:** If a list contains event-only requirements, it should be used with `Register()` for reactive evaluation. Calling `Evaluate()` imperatively on such a list will return incorrect results (the event-only requirements will fail).

`ValidateRequirements` does not detect event-only requirements in imperatively-evaluated lists at this time — this is a known limitation. Document clearly in the Data Asset which evaluation path the list supports.

---

# Writing a New Requirement Type

```cpp
// 1. Place in the module that owns the data being queried.
//    Example: Tags/Requirements/Requirement_HasTag.h

UCLASS(DisplayName = "Has Gameplay Tag")
class URequirement_HasTag : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirement")
    FGameplayTag RequiredTag;

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override
    {
        // Accept any context struct that carries a PlayerState.
        // Define FPlayerContext in a shared header all requirements can include.
        const FPlayerContext* Ctx = Context.Data.GetPtr<FPlayerContext>();
        if (!Ctx || !Ctx->PlayerState)
            return FRequirementResult::Fail(LOCTEXT("NoContext", "No player context."));

        UAbilitySystemComponent* ASC =
            Ctx->PlayerState->FindComponentByClass<UAbilitySystemComponent>();
        if (!ASC || !ASC->HasMatchingGameplayTag(RequiredTag))
        {
            return FRequirementResult::Fail(
                FText::Format(LOCTEXT("MissingTag", "Missing tag: {0}"),
                    FText::FromName(RequiredTag.GetTagName())));
        }
        return FRequirementResult::Pass();
    }

    virtual void GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Tag.TagAdded"));
        OutEvents.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Tag.TagRemoved"));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Has Tag: %s"), *RequiredTag.ToString());
    }
#endif
};
```

---

# Common Mistakes

| Mistake | Consequence | Fix |
|---|---|---|
| Calling `URequirementLibrary::EvaluateAll` directly | Bypasses list operator and authority | Call `List->Evaluate(Ctx)` |
| Not binding `OnResultChanged` before `Register()` | Misses initial evaluation callback | Bind first, then Register |
| Forgetting `Unregister()` | Stale weak ptr in watcher, cleaned lazily | Always Unregister at teardown |
| Adding typed fields to `FRequirementContext` | Breaks zero-dependency rule | Put data in `FInstancedStruct Data` |
| Storing per-player state on `URequirement` | Data corruption across concurrent evaluations | URequirement is stateless — move state to the caller |
| Imperatively evaluating a list with event-only requirements | Event-only requirements return Fail | Use `Register()` + `OnResultChanged` for such lists |
| Mutating the requirement instance inside `Evaluate` | Thread-safety violation, state corruption | `Evaluate` must be `const` and stateless |
