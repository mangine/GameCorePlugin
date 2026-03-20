# Requirement Reactive Evaluation

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

Reactive evaluation — being notified when a requirement list's pass/fail state changes — is handled directly by `URequirementList` via `RegisterWatch` and `UnregisterWatch`. There is no separate helper class.

`URequirementList::RegisterWatch` is a thin method that:
1. Collects watched tags from all requirements via `CollectWatchedEvents`.
2. Maps `this->Authority` to `EGameCoreEventScope`.
3. Registers a closure with `UGameCoreEventWatcher` that evaluates the list on event arrival and calls the caller's `OnResult` delegate only when pass/fail state changes.

The caller captures any private context (quest ID, objective ID, etc.) in the `OnResult` closure. `RegisterWatch` never inspects it.

---

# Why `RegisterWatch` Lives on `URequirementList`

The registration logic belongs on the object being registered. Consuming systems can discover `RegisterWatch` directly from the list they already hold — no need to know about a separate helper class. All reactive behaviour for a list is discoverable in one place.

The routing infrastructure (`UGameCoreEventWatcher`) remains fully generic and reusable by any system, independent of requirements.

---

# Registration Flow

```
Consuming system calls:
  List->RegisterWatch(this, [WeakThis, MyKey](bool bPassed) { ... })
    │
    ├─ CollectWatchedEvents() → FGameplayTagContainer
    ├─ AuthorityToScope()     → EGameCoreEventScope
    └─ UGameCoreEventWatcher::Register(Owner, Tags, Scope, Closure)
          │
          └─ Lazy bus subscription per leaf tag
          └─ Returns FEventWatchHandle

Returns FEventWatchHandle to consuming system.

When a watched event fires on UGameCoreEventBus:
  UGameCoreEventWatcher receives it
  Checks scope → skips if net role does not match
  Calls closure:
    Wraps payload in FRequirementContext
    Calls List->EvaluateFromEvent(Ctx)
    If pass/fail changed → calls OnResult(bPassed)
      └─ Consuming system's closure fires with captured context
```

---

# Usage — Quest Component

```cpp
// In UQuestComponent — tracking availability for a specific quest.
void UQuestComponent::StartWatchingAvailability(
    URequirementList* List, FGameplayTag QuestId)
{
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

    ActiveWatchHandles.Add(QuestId, Handle);
}

void UQuestComponent::StopWatchingAvailability(FGameplayTag QuestId)
{
    if (FEventWatchHandle* Handle = ActiveWatchHandles.Find(QuestId))
    {
        URequirementList::UnregisterWatch(this, *Handle);
        ActiveWatchHandles.Remove(QuestId);
    }
}
```

---

# Usage — Non-Requirement System

Systems that need closure-with-context event routing but do not use requirements use `UGameCoreEventWatcher` directly:

```cpp
// An objective tracker responding to a combat event,
// needing to know which objective to update without passing it through the bus.
void UObjectiveTracker::BeginTracking(FGameplayTag ObjectiveId)
{
    TWeakObjectPtr<UObjectiveTracker> WeakThis = this;

    WatchHandle = UGameCoreEventWatcher::Get(this)->Register(
        this,
        FGameplayTag::RequestGameplayTag("RequirementEvent.Combat.EnemyKilled"),
        EGameCoreEventScope::ServerOnly,
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

# Responsibility Map

| Concern | Owner |
|---|---|
| Broadcasting events | Any system via `UGameCoreEventBus` |
| Tag subscription lifecycle | `UGameCoreEventWatcher` |
| Scope enforcement | `UGameCoreEventWatcher::PassesScopeCheck` |
| Closure building + context capture | Caller (quest component, tracker, etc.) |
| Requirement evaluation on event | Closure registered by `URequirementList::RegisterWatch` |
| Pass/fail change detection | `TSharedPtr<TOptional<bool>>` in `RegisterWatch` closure |
| Caller-private context | Captured in caller's `OnResult` lambda |
