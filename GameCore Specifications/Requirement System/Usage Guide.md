# Usage Guide

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

This guide covers the three usage patterns for the Requirement System: one-shot imperative evaluation, reactive watched evaluation, and payload-injected evaluation for persisted data. Read this before implementing a new consuming system.

---

# Pattern 1 — One-Shot Imperative Evaluation

Use when a system needs to check a gate at a specific moment: confirming an RPC, validating an action before executing it, responding to a player request.

**When to use:** Interaction confirmation, ability activation, crafting, purchase validation, any server RPC handler that gates a gameplay action.

## Flow

```
Server RPC received
  → Build FRequirementContext from RPC connection
  → Call List->Evaluate(Context)  [or EvaluateAsync for async requirements]
  → If Pass: execute the action
  → If Fail: send ClientRPC with FRequirementResult.FailureReason
```

## Synchronous Example

```cpp
// Server-side RPC handler confirming a mine action.
void UOreNodeComponent::Server_RequestMine_Implementation(APlayerController* Instigator)
{
    // Always construct context server-side. Never trust client-provided references.
    FRequirementContext Ctx;
    Ctx.PlayerState = Instigator->GetPlayerState<APlayerState>();
    Ctx.World       = GetWorld();
    Ctx.Instigator  = Instigator->GetPawn();

    FRequirementResult Result = NodeDefinition->MineRequirements->Evaluate(Ctx);
    if (!Result.bPassed)
    {
        // Send failure reason to the requesting client only.
        Instigator->ClientRPC_ShowRequirementFailure(Result.FailureReason);
        return;
    }

    ExecuteMine(Ctx.PlayerState);
}
```

## Async Example

```cpp
void UEntitlementGate::Server_RequestUnlock_Implementation(APlayerController* PC)
{
    FRequirementContext Ctx;
    Ctx.PlayerState = PC->GetPlayerState<APlayerState>();
    Ctx.World       = GetWorld();

    // List contains a URequirement_BackendEntitlement — must use async path.
    UnlockDefinition->Requirements->EvaluateAsync(Ctx,
        [this, PC](FRequirementResult Result)
        {
            if (!Result.bPassed)
            {
                PC->ClientRPC_ShowRequirementFailure(Result.FailureReason);
                return;
            }
            GrantUnlock(PC->GetPlayerState<APlayerState>());
        });
    // Do not proceed past this point — wait for the callback.
}
```

**Rules:**
- Always construct `FRequirementContext` on the server from the RPC connection. Never use a client-provided actor reference as the subject.
- For async evaluation, do not execute the gated action outside the callback.
- Use `ValidateRequirements` at `BeginPlay` on every requirement array your system holds, to catch authoring errors early.

---

# Pattern 2 — Reactive Watched Evaluation

Use when a system needs to know *when* requirements become met or unmet over time, without polling. Typical use: quest availability, ability unlock visibility, UI state.

**When to use:** Any system that shows "requirements met" UI, unlocks features reactively, or needs to respond to player state changes without a timer.

## Flow

```
System initialises (after BeginPlay)
  → RegisterSet(List, OnDirty, ContextBuilder?)
  → Store FRequirementSetHandle

Player state changes (level up, item gained, quest completed, ...)
  → Owning module fires WatcherManager->NotifyPlayerEvent(PlayerState, EventTag)
  → WatcherComponent marks watching sets dirty
  → Coalescing timer fires FlushPendingEvaluations()
  → Each dirty set: ContextBuilder called, then List->Evaluate(Context)
  → OnDirty(Handle, bAllPassed) fires on owning system
  → System reacts (show unlock UI, enable button, progress quest)

System tears down
  → UnregisterSet(Handle)
```

## Registration Example

```cpp
void UQuestAvailabilityTracker::BeginTracking(URequirementList* List)
{
    URequirementWatcherComponent* Watcher =
        PlayerState->FindComponentByClass<URequirementWatcherComponent>();
    check(Watcher);

    // No ContextBuilder needed — requirements read directly from PlayerState.
    WatchHandle = Watcher->RegisterSet(
        List,
        FOnRequirementSetDirty::CreateUObject(this, &UQuestAvailabilityTracker::OnAvailabilityChanged)
    );
}

void UQuestAvailabilityTracker::OnAvailabilityChanged(FRequirementSetHandle Handle, bool bAllPassed)
{
    if (bAllPassed)
        BroadcastQuestAvailable();
    else
        BroadcastQuestUnavailable();
}

void UQuestAvailabilityTracker::StopTracking()
{
    if (URequirementWatcherComponent* Watcher =
        PlayerState->FindComponentByClass<URequirementWatcherComponent>())
    {
        Watcher->UnregisterSet(WatchHandle);
    }
    WatchHandle = {};
}
```

## Firing Invalidation Events

Every system that changes state requirements may watch must fire the appropriate tag:

```cpp
// In ULevelingComponent, after granting XP that caused a level-up:
void ULevelingComponent::OnLevelUp(int32 NewLevel)
{
    // ... apply level-up effects ...

    // Notify all watcher components watching level-related requirements.
    if (URequirementWatcherManager* Mgr = GetWorld()->GetSubsystem<URequirementWatcherManager>())
    {
        Mgr->NotifyPlayerEvent(
            GetOwner<APlayerState>(),
            FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"));
    }
}
```

**Rules:**
- Always store the `FRequirementSetHandle`. Discarding it means you cannot unregister.
- Always call `UnregisterSet` when the tracking is no longer needed — the component cleans up on `EndPlay` but mid-session leaks waste subscription slots.
- Never call `RegisterSet` or `UnregisterSet` from inside a `ContextBuilder` lambda.
- Tune `FlushDelaySeconds` per set: 0.5s is the default (acceptable for quest unlock); use 0.1s for UI that needs to feel responsive.

---

# Pattern 3 — Payload-Injected Evaluation (Persisted Data)

Use when a requirement needs to read runtime counter or float data that is owned by the consuming system and not derivable from world state alone. Typical use: quest kill counts, delivery counts, time-elapsed trackers.

**When to use:** Any requirement that checks "has the player done X amount of Y" where X is tracked by the owning system, not by a world-state query.

## Flow

```
Owning system holds runtime tracker data (e.g. FQuestRuntime with kill count)
  → Before Evaluate: build FRequirementPayload from tracker data
  → Insert payload into FRequirementContext::PersistedData under domain tag
  → Call List->Evaluate(Context)
  → URequirement_Persisted subclass reads payload via its PayloadKey
```

## Imperative Payload Example

```cpp
// UQuestComponent confirming stage completion server-side:
void UQuestComponent::CheckStageCompletion(const FQuestRuntime& QuestRuntime)
{
    FRequirementContext Ctx;
    Ctx.PlayerState = GetOwner<APlayerState>();
    Ctx.World       = GetWorld();

    // Build payload from the quest's tracker data.
    FRequirementPayload Payload;
    for (const FQuestTrackerEntry& Tracker : QuestRuntime.Trackers)
        Payload.Counters.Add(Tracker.TrackerKey, Tracker.CurrentValue);

    // Inject under the quest's domain tag so requirements can find it.
    Ctx.PersistedData.Add(QuestRuntime.QuestId, Payload);

    FRequirementResult Result = QuestRuntime.StageDef->CompletionRequirements->Evaluate(Ctx);
    if (Result.bPassed)
        AdvanceToNextStage(QuestRuntime);
}
```

## Watcher ContextBuilder Example

For reactive evaluation, the payload is injected via the `ContextBuilder` delegate passed to `RegisterSet`:

```cpp
void UQuestComponent::RegisterCompletionWatcher(const FQuestRuntime& QuestRuntime)
{
    URequirementWatcherComponent* Watcher =
        GetOwner<APlayerState>()->FindComponentByClass<URequirementWatcherComponent>();

    FGameplayTag QuestId = QuestRuntime.QuestId;
    TWeakObjectPtr<UQuestComponent> WeakThis = this;

    FRequirementSetHandle Handle = Watcher->RegisterSet(
        QuestRuntime.StageDef->CompletionRequirements,
        FOnRequirementSetDirty::CreateUObject(this, &UQuestComponent::OnCompletionWatcherDirty),
        // ContextBuilder: inject tracker payload before every flush evaluation.
        [WeakThis, QuestId](FRequirementContext& Ctx)
        {
            UQuestComponent* QC = WeakThis.Get();
            if (!QC) return;

            const FQuestRuntime* QR = QC->FindActiveQuest(QuestId);
            if (!QR) return;

            FRequirementPayload Payload;
            for (const FQuestTrackerEntry& T : QR->Trackers)
                Payload.Counters.Add(T.TrackerKey, T.CurrentValue);

            Ctx.PersistedData.Add(QuestId, Payload);
        }
    );

    ActiveWatchHandles.Add(QuestId, Handle);
}
```

## Implementing a `URequirement_Persisted` Subclass

```cpp
// Checks that a specific tracker counter meets a minimum value.
// Payload is injected by the owning quest system under the quest's domain tag.
UCLASS(DisplayName = "Tracker Count")
class URequirement_TrackerCount : public URequirement_Persisted
{
    GENERATED_BODY()
public:
    // Which counter within the payload to check.
    UPROPERTY(EditDefaultsOnly, Category = "Requirement",
        meta = (Categories = "Quest.Counter"))
    FGameplayTag CounterKey;

    // Minimum value required to pass.
    UPROPERTY(EditDefaultsOnly, Category = "Requirement", meta = (ClampMin = "1"))
    int32 RequiredCount = 1;

    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        // Payload is built from replicated FQuestRuntime — available on both sides.
        return ERequirementDataAuthority::Both;
    }

    virtual FRequirementResult EvaluateWithPayload(
        const FRequirementContext& Context,
        const FRequirementPayload& Payload) const override
    {
        int32 Current = 0;
        if (!Payload.GetCounter(CounterKey, Current))
        {
            return FRequirementResult::Fail(
                FText::Format(LOCTEXT("TrackerMissing", "Tracker '{0}' not found."),
                    FText::FromName(CounterKey.GetTagName())));
        }

        if (Current < RequiredCount)
        {
            return FRequirementResult::Fail(
                FText::Format(LOCTEXT("TrackerInsufficient", "{0} / {1}"),
                    FText::AsNumber(Current), FText::AsNumber(RequiredCount)));
        }
        return FRequirementResult::Pass();
    }

    virtual void GetWatchedEvents_Implementation(FGameplayTagContainer& OutEvents) const override
    {
        // Invalidated when any quest tracker value changes.
        OutEvents.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Quest.TrackerUpdated"));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("%s >= %d"), *CounterKey.ToString(), RequiredCount);
    }
#endif
};
```

**Rules:**
- `PayloadKey` is inherited from `URequirement_Persisted` — it is the domain tag (e.g. QuestId). It must match the key injected into `PersistedData` by the owning system's `ContextBuilder`.
- `CounterKey` is the inner tag within the payload — the specific counter name.
- Never subclass `URequirement` directly when you need payload data. Always go through `URequirement_Persisted` so the domain lookup is sealed and consistent.

---

# Adding a New Requirement Type — Full Checklist

```
1. Subclass URequirement (or URequirement_Persisted if reading payload data).
   File lives inside the module that owns the data being queried:
     Quest data       → Quest/Requirements/
     Inventory data   → Inventory/Requirements/
     Leveling data    → Leveling/Requirements/
     Tag data         → Tags/Requirements/
     Base logic only  → Requirements/

2. UCLASS macro:
   UCLASS(DisplayName = "Human Readable Name", Blueprintable)
   Do NOT add Abstract. Do NOT add EditInlineNew (inherited).

3. Add "Requirements" to PublicDependencyModuleNames in Build.cs.

4. Config properties:
   UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirement")
   No mutable state. No TObjectPtr to runtime actors.

5. Override GetDataAuthority().
   Be precise — ClientOnly only if all data is replicated,
   ServerOnly if any data is server-only, Both only if genuinely correct on both sides.

6. Override Evaluate() for sync, or IsAsync() + EvaluateAsync() for async.
   Async: always wrap OnComplete with MakeGuardedCallback before any early-return path.
   Persisted: override EvaluateWithPayload() instead — never override Evaluate().

7. Override GetWatchedEvents_Implementation().
   Return empty if on-demand only (watcher will never watch this type).
   Cache FGameplayTag handles — use AddNativeGameplayTag at module startup.

8. Set bIsMonotonic = true in CDO constructor if the condition can never revert.
   Common: QuestCompleted, MinLevel once a minimum is reached.
   Never: item possession, tag presence, cooldowns.

9. Override GetDescription() inside #if WITH_EDITOR.
   Include the configured property values: "MinLevel >= 20", "Has Tag: Status.Ready".

10. No registration step. UE reflection discovers the subclass automatically.
```

---

# Common Mistakes

| Mistake | Consequence | Correct approach |
|---|---|---|
| Calling `URequirementLibrary::EvaluateAll` directly | Bypasses list operator and authority validation | Call `List->Evaluate(Context)` |
| Capturing `FRequirementContext` by reference in async lambda | Dangling reference — context is a stack variable | Capture specific values from context |
| Not calling `MakeGuardedCallback` in `EvaluateAsync` | No timeout, possible double-fire | Call `MakeGuardedCallback` before any early return |
| Adding a typed component pointer to `FRequirementContext` | Breaks `Requirements/` zero-dependency rule | Use `PlayerState->FindComponentByClass<T>()` inside the requirement |
| Subclassing `URequirement` for payload data without going through `URequirement_Persisted` | Inconsistent domain key lookup | Subclass `URequirement_Persisted`, override `EvaluateWithPayload` |
| Marking a reversible condition `bIsMonotonic` | Cache never invalidates, requirement is permanently stuck at Pass | Only set on conditions that are logically permanent |
| Discarding `FRequirementSetHandle` | Cannot unregister, leaks subscription until PlayerState EndPlay | Always store the handle |
| Calling `RegisterSet` from inside a `ContextBuilder` | Re-entrant watcher mutation, undefined behaviour | Defer registration to after flush completes |
