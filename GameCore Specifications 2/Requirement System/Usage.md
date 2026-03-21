# Requirement System — Usage Guide

Practical integration patterns for the Requirement System.

---

## Pattern 1 — Imperative One-Shot Check

Use when a system needs to validate a gate at a specific moment: confirming an RPC, checking interaction eligibility, validating an action.

**When to use:** Server RPC handlers, interaction confirmation, ability activation, crafting validation.

### Flow

```
Build FRequirementContext from current world state
  → List->Evaluate(Ctx)
  → FRequirementResult { bPassed, FailureReason }
  → On Fail: send FailureReason to client via targeted RPC
  → On Pass: execute the action
```

### Example — Server RPC confirming a mine action

```cpp
// Snapshot struct defined in the module that owns the data being queried.
// Lives in e.g. Leveling/LevelingTypes.h — NOT in Requirements/.
USTRUCT()
struct FMineActionContext
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() TObjectPtr<UWorld>       World       = nullptr;
};

// Server RPC handler on an ore node component:
void UOreNodeComponent::Server_RequestMine_Implementation(APlayerController* Instigator)
{
    FMineActionContext CtxData;
    CtxData.PlayerState = Instigator->GetPlayerState<APlayerState>();
    CtxData.World       = GetWorld();

    FRequirementContext Ctx = FRequirementContext::Make(CtxData);
    FRequirementResult  Result = NodeDefinition->MineRequirements->Evaluate(Ctx);

    if (!Result.bPassed)
    {
        // Send localised failure reason to the requesting client only.
        Instigator->ClientRPC_ShowRequirementFailure(Result.FailureReason);
        return;
    }
    ExecuteMine(CtxData.PlayerState);
}
```

**Rules:**
- Always build context server-side from the validated RPC connection. Never reuse client-supplied actor references as the evaluation subject.
- Call `URequirementLibrary::ValidateRequirements(List->Requirements, List->Authority)` at `BeginPlay` on every list your system holds, to catch authoring errors early in development.
- `Evaluate` on an empty requirement list returns `Pass` immediately.

---

## Pattern 2 — Reactive Watched Evaluation

Use when a system needs to know when requirements become met or unmet over time, without polling.

**When to use:** Quest availability tracking, ability unlock visibility, UI state, NPC dialogue gates.

### Flow

```
Setup:
  List->RegisterWatch(this, [WeakThis, Key](bool bPassed) { ... })
  Store returned FEventWatchHandle
  [Optionally: call List->Evaluate(Ctx) once for an immediate baseline result]

During play:
  Any system fires UGameCoreEventBus::Broadcast(RequirementEvent.*, Payload)
  UGameCoreEventWatcher delivers to the closure registered by RegisterWatch
  Closure calls EvaluateFromEvent → if pass/fail changed → OnResult(bPassed) fires
  Consuming system acts on the result via captured context in the lambda

Teardown:
  List->UnregisterWatch(this, Handle)
```

### Example — Quest availability tracking

```cpp
// In UQuestComponent.h:
TMap<FGameplayTag, FEventWatchHandle> AvailabilityWatches;

// Setup:
void UQuestComponent::StartWatchingAvailability(
    URequirementList* List, FGameplayTag QuestId)
{
    check(List);

    TWeakObjectPtr<UQuestComponent> WeakThis = this;

    FEventWatchHandle Handle = List->RegisterWatch(this,
        [WeakThis, QuestId](bool bPassed)
        {
            if (UQuestComponent* Self = WeakThis.Get())
            {
                if (bPassed) Self->OnQuestAvailable(QuestId);
                else         Self->OnQuestUnavailable(QuestId);
            }
        });

    AvailabilityWatches.Add(QuestId, Handle);

    // Optional: get an immediate baseline before the first event arrives.
    FMyPlayerContext BaselineCtx = BuildPlayerContext();
    FRequirementResult Baseline = List->Evaluate(FRequirementContext::Make(BaselineCtx));
    OnQuestAvailabilityChanged(QuestId, Baseline.bPassed);
}

// Teardown:
void UQuestComponent::StopWatchingAvailability(FGameplayTag QuestId)
{
    if (FEventWatchHandle* Handle = AvailabilityWatches.Find(QuestId))
    {
        URequirementList::UnregisterWatch(this, *Handle);
        AvailabilityWatches.Remove(QuestId);
    }
}
```

### Example — Firing the invalidation event from the Leveling System

```cpp
void ULevelingComponent::OnLevelUp(APlayerState* PS, int32 OldLevel, int32 NewLevel)
{
    // Apply level-up effects ...

    FLevelChangedEvent Payload;
    Payload.PlayerState = PS;
    Payload.OldLevel    = OldLevel;
    Payload.NewLevel    = NewLevel;

    UGameCoreEventBus::Get(this)->Broadcast(
        FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"),
        FInstancedStruct::Make(Payload),
        EGameCoreEventScope::ServerOnly);
    // No direct call to any requirement system — the watcher handles routing.
}
```

**Rules:**
- Always use `TWeakObjectPtr` for any `UObject` captured in the `OnResult` lambda.
- `RegisterWatch` returns an invalid handle if the list has no watched events — check before storing.
- `UnregisterWatch` is safe to call with an invalid handle.
- The closure fires only when pass/fail **changes**, not on every event. Rapid event bursts do not spam callbacks.

---

## Pattern 3 — ClientValidated Flow

Use when immediate client UI feedback is needed, but the gate must be authoritative.

```
Client side:
  List (Authority = ClientValidated) is evaluated locally via Evaluate(ClientCtx)
  On local pass: fire a Server RPC

Server side (RPC handler):
  Rebuild FRequirementContext from the RPC connection — never trust client data
  Call List->Evaluate(ServerCtx)
  On server pass: execute the action, notify client
  On server fail: send FailureReason to client
```

```cpp
// Client fires optimistic RPC:
void UMyComponent::TryActivate()
{
    // Quick local check for UI responsiveness.
    FMyContext LocalCtx = BuildLocalContext();
    FRequirementResult LocalResult = ActivationList->Evaluate(FRequirementContext::Make(LocalCtx));
    if (!LocalResult.bPassed) return; // Don't even bother sending RPC

    Server_RequestActivate();
}

// Server RPC:
void UMyComponent::Server_RequestActivate_Implementation()
{
    // Re-evaluate from scratch on the server. Never trust the client.
    FMyContext ServerCtx = BuildServerContext();
    FRequirementResult Result = ActivationList->Evaluate(FRequirementContext::Make(ServerCtx));
    if (!Result.bPassed)
    {
        // List->Authority must be ClientValidated for this pattern.
        Client_ShowFailure(Result.FailureReason);
        return;
    }
    ExecuteActivation();
}
```

---

## Writing a New Requirement Type

```cpp
// Place in the module that owns the data being queried.
// Example: Leveling/Requirements/Requirement_MinLevel.h

UCLASS(DisplayName = "Minimum Player Level")
class URequirement_MinLevel : public URequirement
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirement",
        meta = (ClampMin = "1"))
    int32 MinLevel = 1;

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override
    {
        // Accept both a snapshot struct and the event payload — both carry level data.
        int32 CurrentLevel = 0;

        if (const FLevelingContext* Snap = Context.Data.GetPtr<FLevelingContext>())
            CurrentLevel = Snap->CurrentLevel;
        else if (const FLevelChangedEvent* Evt = Context.Data.GetPtr<FLevelChangedEvent>())
            CurrentLevel = Evt->NewLevel;
        else
            return FRequirementResult::Fail(LOCTEXT("BadCtx", "Unexpected context type."));

        if (CurrentLevel < MinLevel)
        {
            return FRequirementResult::Fail(
                FText::Format(LOCTEXT("MinLevel_Fail", "Requires level {0}."),
                    FText::AsNumber(MinLevel)));
        }
        return FRequirementResult::Pass();
    }

    virtual void GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("MinLevel >= %d"), MinLevel);
    }
#endif
};
```

### Requirement Author Checklist

```
1. Subclass URequirement in the module that owns the queried data.
   Leveling data  → Leveling/Requirements/
   Tag data       → Tags/Requirements/
   Quest data     → Quest/Requirements/
   Generic logic  → Requirements/

2. UCLASS macro: add DisplayName. Never add Abstract.

3. Declare config properties: EditDefaultsOnly, BlueprintReadOnly.
   No mutable state. No runtime pointers.

4. Override Evaluate(FRequirementContext) for imperative support.
   Cast Context.Data to the expected struct type.
   Handle unknown struct types with a clear Fail reason.

5. Override EvaluateFromEvent(FRequirementContext) only if event
   behaviour differs from snapshot behaviour. Default delegates to Evaluate.

6. Override GetWatchedEvents_Implementation to return all
   RequirementEvent.* tags that can invalidate this requirement.
   Return empty if never used in watched lists.

7. Override GetDescription() inside #if WITH_EDITOR.
   Include configured values: "MinLevel >= 20".

8. No registration step — UE reflection discovers the subclass automatically.
```

---

## Common Mistakes

| Mistake | Consequence | Fix |
|---|---|---|
| Calling `URequirementLibrary::EvaluateAll` directly | Bypasses list operator and authority | Call `List->Evaluate(Ctx)` |
| Not calling `Evaluate` imperatively for initial baseline | First reactive callback may not fire until the first event | Call `Evaluate` once at setup for an immediate result |
| Forgetting `UnregisterWatch` at teardown | Stale closure kept alive in watcher | Call `UnregisterWatch` in `EndPlay` or equivalent |
| Adding typed fields to `FRequirementContext` | Violates zero-dependency rule | All domain data lives inside `FInstancedStruct Data` |
| Storing per-player state on `URequirement` | Corruption across concurrent evaluations | `URequirement` is stateless — move state to the caller |
| Imperatively evaluating a list with event-only requirements | Event-only requirements return Fail | Use separate lists, or document the list as reactive-only |
| Capturing a raw `this` pointer in the lambda | Crash if object is destroyed before the event fires | Always use `TWeakObjectPtr<>` |
| Calling `List->Evaluate` from the wrong net role | Incorrect result on the wrong machine | Authority is declared on the asset; respect it |
