# Requirement Watch Helper

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

The Requirement System's reactive evaluation path is built on top of `UGameCoreEventWatcher`. The requirement system does not own a watcher subsystem — it owns a **helper** (`URequirementWatchHelper`) that registers closures with `UGameCoreEventWatcher` on behalf of a consuming system.

The helper is a convenience layer. Systems that want full control can register with `UGameCoreEventWatcher` directly and call `List->EvaluateFromEvent` in their own callback.

**File:** `Requirements/RequirementWatchHelper.h / .cpp`

---

# Why a Helper and Not a Subsystem

`URequirementWatcherManager` was previously a `UWorldSubsystem` that duplicated event routing logic already owned by `UGameCoreEventWatcher`. It added no value beyond what the generic watcher provides. It was removed.

The helper replaces it with a thin registration utility. It:
1. Collects watched tags from the list's requirements via `GetWatchedEvents`.
2. Builds a closure that wraps the event payload in `FRequirementContext`, calls `List->EvaluateFromEvent`, and calls a user-supplied `TFunction<void(bool)>` delegate with the pass/fail result.
3. Registers that closure with `UGameCoreEventWatcher` for each watched tag.
4. Returns a single `FEventWatchHandle` covering all tag registrations.

The user-supplied delegate captures caller-private context — quest ID, component pointer, whatever the consuming system needs — without exposing it to the watcher or the requirement system.

---

# `URequirementWatchHelper`

```cpp
UCLASS()
class GAMECORE_API URequirementWatchHelper : public UObject
{
    GENERATED_BODY()
public:

    // Registers a requirement list for reactive evaluation.
    //
    // When any of the list's watched event tags fires:
    //   1. The event payload is wrapped in FRequirementContext.
    //   2. List->EvaluateFromEvent(Context) is called.
    //   3. If the pass/fail result changed since the last evaluation,
    //      OnResult(bPassed) is called.
    //
    // OnResult captures caller context via closure — the helper never inspects it.
    // Use TWeakObjectPtr for any UObject captured in OnResult.
    //
    // Authority is read from List->Authority. The helper skips evaluation
    // silently if called on the wrong network side.
    //
    // Returns one FEventWatchHandle covering all tag subscriptions for this list.
    // Pass to Unregister when reactive tracking is no longer needed.
    static FEventWatchHandle RegisterList(
        const UObject* Owner,
        URequirementList* List,
        TFunction<void(bool /*bPassed*/)> OnResult);

    // Removes all tag subscriptions established by the given handle.
    static void UnregisterList(
        const UObject* Owner,
        FEventWatchHandle Handle);
};
```

---

# `RegisterList` — Implementation

```cpp
FEventWatchHandle URequirementWatchHelper::RegisterList(
    const UObject* Owner,
    URequirementList* List,
    TFunction<void(bool)> OnResult)
{
    if (!List || !OnResult || !Owner) return FEventWatchHandle{};

    UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(Owner);
    if (!Watcher) return FEventWatchHandle{};

    // Collect all watched tags from the list's requirements.
    FGameplayTagContainer WatchedTags;
    List->CollectWatchedEvents(WatchedTags);
    if (WatchedTags.IsEmpty()) return FEventWatchHandle{};

    // Shared state between the closures for this registration:
    // tracks last known result so OnResult only fires on state change.
    auto LastResult = MakeShared<TOptional<bool>>();

    TWeakObjectPtr<URequirementList> WeakList = List;
    ERequirementEvalAuthority Authority = List->Authority;

    return Watcher->Register(Owner, WatchedTags,
        [WeakList, OnResult, LastResult, Authority]
        (FGameplayTag Tag, const FInstancedStruct& Payload)
        {
            URequirementList* L = WeakList.Get();
            if (!L) return;

            // Authority check.
            if (!URequirementWatchHelper::PassesAuthority(L)) return;

            // Wrap event payload as evaluation context.
            FRequirementContext Ctx;
            Ctx.Data = Payload;

            FRequirementResult Result = L->EvaluateFromEvent(Ctx);

            // Only fire OnResult if pass/fail state changed.
            if (!LastResult->IsSet() || LastResult->GetValue() != Result.bPassed)
            {
                *LastResult = Result.bPassed;
                OnResult(Result.bPassed);
            }
        });
}

void URequirementWatchHelper::UnregisterList(
    const UObject* Owner,
    FEventWatchHandle Handle)
{
    if (UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(Owner))
        Watcher->Unregister(Handle);
}
```

---

# Authority Check

```cpp
// static
bool URequirementWatchHelper::PassesAuthority(const URequirementList* List)
{
    const UWorld* World = List->GetWorld();
    if (!World) return false;

    ENetMode NetMode = World->GetNetMode();
    switch (List->Authority)
    {
    case ERequirementEvalAuthority::ServerOnly:
        return NetMode != NM_Client;
    case ERequirementEvalAuthority::ClientOnly:
        return NetMode == NM_Client || NetMode == NM_Standalone;
    case ERequirementEvalAuthority::ClientValidated:
        return true;
    }
    return false;
}
```

---

# Usage — Quest Component Example

This demonstrates the full pattern: the consuming system captures its own context in the `OnResult` closure. The helper and watcher see only `bool bPassed`.

```cpp
// In UQuestComponent, tracking availability for a specific quest:
void UQuestComponent::StartWatchingAvailability(URequirementList* List, FGameplayTag QuestId)
{
    TWeakObjectPtr<UQuestComponent> WeakThis = this;

    FEventWatchHandle Handle = URequirementWatchHelper::RegisterList(
        this,
        List,
        // OnResult closure — captures QuestId privately.
        // The helper knows nothing about QuestId or UQuestComponent.
        [WeakThis, QuestId](bool bPassed)
        {
            if (UQuestComponent* Self = WeakThis.Get())
            {
                if (bPassed)
                    Self->OnQuestAvailable(QuestId);
                else
                    Self->OnQuestUnavailable(QuestId);
            }
        });

    // Store handle keyed by QuestId for later unregistration.
    ActiveWatchHandles.Add(QuestId, Handle);
}

void UQuestComponent::StopWatchingAvailability(FGameplayTag QuestId)
{
    if (FEventWatchHandle* Handle = ActiveWatchHandles.Find(QuestId))
    {
        URequirementWatchHelper::UnregisterList(this, *Handle);
        ActiveWatchHandles.Remove(QuestId);
    }
}
```

---

# Usage — Non-Requirement System Using UGameCoreEventWatcher Directly

Systems that don't use requirements but still need closure-with-context event callbacks skip the helper entirely and use `UGameCoreEventWatcher` directly:

```cpp
// An objective tracker that needs to know which objective to update
// when a combat event fires, without passing objective ID through the event bus.

void UObjectiveTracker::BeginTracking(FGameplayTag ObjectiveId)
{
    TWeakObjectPtr<UObjectiveTracker> WeakThis = this;

    FGameplayTag KillTag =
        FGameplayTag::RequestGameplayTag("RequirementEvent.Combat.EnemyKilled");

    WatchHandle = UGameCoreEventWatcher::Get(this)->Register(this, KillTag,
        [WeakThis, ObjectiveId](FGameplayTag, const FInstancedStruct& Payload)
        {
            if (UObjectiveTracker* Self = WeakThis.Get())
            {
                const FEnemyKilledEvent* Evt = Payload.GetPtr<FEnemyKilledEvent>();
                if (Evt) Self->OnEnemyKilled(ObjectiveId, *Evt);
            }
        });
}

void UObjectiveTracker::StopTracking()
{
    UGameCoreEventWatcher::Get(this)->Unregister(WatchHandle);
}
```

---

# Summary of Responsibilities

| Concern | Owner |
|---|---|
| Broadcasting events | Owning system via `UGameCoreEventBus` |
| Tag subscription lifecycle | `UGameCoreEventWatcher` |
| Closure building and context capture | Caller (quest component, tracker, etc.) |
| Requirement evaluation on event | `URequirementWatchHelper` closure |
| Authority enforcement | `URequirementWatchHelper::PassesAuthority` |
| Pass/fail change detection | `URequirementWatchHelper` shared state in closure |
| Caller-private context (quest ID, etc.) | Captured in caller's `OnResult` lambda |
