# Watcher System

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

The Watcher System provides event-driven reactive evaluation for requirement sets. It eliminates polling by inverting control: instead of systems asking "are requirements met?" on a timer, requirements declare which events invalidate them. When those events fire, only affected sets are re-evaluated.

**Files:** `Requirements/RequirementWatcher.h / .cpp`

---

# Problem It Solves

In an MMORPG with 1,000 players and 500 quest definitions, polling every player's requirements every time any state changes is O(players × quests) — unacceptable. The Watcher System reduces per-event cost to O(sets watching that specific event for the affected player), typically 5–20 sets rather than 500,000 checks.

---

# Architecture

Two classes with distinct responsibilities:

| Class | Type | Lives On | Responsibility |
| --- | --- | --- | --- |
| `URequirementWatcherManager` | `UWorldSubsystem` | Server + owning Client | Receives `RequirementEvent.*` tag broadcasts. Routes them to the correct player's component. |
| `URequirementWatcherComponent` | `UActorComponent` on `APlayerState` | Server + owning Client | Owns all set registrations for one player. Maintains dirty flags and coalescing timer. Fires callbacks to owning systems. |

`URequirementWatcherManager` is the **router**. It never evaluates. `URequirementWatcherComponent` is the **evaluator**. It never knows about specific game systems.

---

# `URequirementWatcherManager`

```cpp
UCLASS()
class GAMECORE_API URequirementWatcherManager : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    // Called by any system when a relevant state change occurs.
    // Tag must be in the RequirementEvent.* namespace.
    // Looks up the player's watcher component and calls MarkEventDirty on it.
    // O(1) lookup via TMap; negligible cost even at 1k players.
    UFUNCTION(BlueprintCallable, Category = "Requirements|Watcher")
    void NotifyPlayerEvent(APlayerState* PlayerState, FGameplayTag EventTag);

    // Variant for world-scoped events with no single player owner.
    // Notifies ALL registered watcher components watching this tag.
    // Use sparingly — O(players watching tag). Prefer per-player notification.
    void NotifyWorldEvent(FGameplayTag EventTag);

    // Called automatically by URequirementWatcherComponent on Begin/EndPlay.
    void RegisterWatcher(APlayerState* PlayerState, URequirementWatcherComponent* Component);
    void UnregisterWatcher(APlayerState* PlayerState);

private:
    TMap<TObjectPtr<APlayerState>, TObjectPtr<URequirementWatcherComponent>> PlayerWatchers;
};
```

**Calling `NotifyPlayerEvent`.** Each system fires this when it changes state that requirements may watch:

```cpp
// In the Leveling system, when a player levels up:
URequirementWatcherManager* WatcherMgr = GetWorld()->GetSubsystem<URequirementWatcherManager>();
WatcherMgr->NotifyPlayerEvent(PlayerState,
    FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"));
```

The manager does not know what "LevelChanged" means. It just routes to the right component.

---

# `URequirementWatcherComponent`

```cpp
UCLASS()
class GAMECORE_API URequirementWatcherComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // ── Registration ─────────────────────────────────────────────────────────

    // Registers a requirement set for reactive tracking.
    // Collects watched events from all requirements in the set.
    // Allocates a FRequirementSetRuntime with a parallel cache array.
    // Returns a handle the owning system must store to unregister later.
    FRequirementSetHandle RegisterSet(
        URequirementSet* Set,
        ERequirementEvalAuthority Authority,
        FOnRequirementSetDirty OnDirty);

    // Removes all subscriptions and cache for this set.
    // Call when the quest completes, the ability is unlocked, or the system no longer
    // needs to track this set. Leaking handles wastes memory and subscription slots.
    void UnregisterSet(FRequirementSetHandle Handle);

    // ── Event Input ──────────────────────────────────────────────────────────

    // Called by URequirementWatcherManager. Marks all sets watching this tag dirty.
    // If no timer is running, starts the coalescing flush timer.
    void MarkEventDirty(FGameplayTag EventTag);

    // ── Manual Evaluation ────────────────────────────────────────────────────

    // Forces immediate evaluation of a specific set, bypassing the flush timer.
    // Use for one-shot checks that cannot wait for the next flush window.
    FRequirementResult EvaluateSetNow(FRequirementSetHandle Handle,
                                      const FRequirementContext& Context);

private:
    // Registered sets, keyed by handle ID.
    TMap<uint32, FRequirementSetRuntime> RegisteredSets;

    // Tag → set handles. Built at RegisterSet time from CollectWatchedEvents.
    TMap<FGameplayTag, TArray<FRequirementSetHandle>> TagToSets;

    // Sets dirtied since last flush. TSet deduplicates multiple dirty marks.
    TSet<FRequirementSetHandle> PendingDirtySet;

    // Flush timer. Started on first dirty mark; invalidated after flush.
    FTimerHandle FlushTimerHandle;

    // Configurable per component instance. Owning systems set this at registration.
    // Default: 0.5s. UI-facing systems may lower to 0.1s.
    float FlushDelaySeconds = 0.5f;

    void StartFlushTimerIfNeeded();
    void FlushPendingEvaluations();
    void EvaluateSetInternal(FRequirementSetRuntime& Runtime);
};
```

---

# Dirty Coalescing

The coalescing mechanism ensures that many rapid state changes (looting 20 items, gaining XP from 10 kills simultaneously) produce **one evaluation per set**, not one per event.

```cpp
void URequirementWatcherComponent::MarkEventDirty(FGameplayTag EventTag)
{
    const TArray<FRequirementSetHandle>* Sets = TagToSets.Find(EventTag);
    if (!Sets) return;

    for (const FRequirementSetHandle& Handle : *Sets)
        PendingDirtySet.Add(Handle); // TSet deduplicates automatically

    StartFlushTimerIfNeeded();
}

void URequirementWatcherComponent::StartFlushTimerIfNeeded()
{
    if (!FlushTimerHandle.IsValid())
    {
        GetWorld()->GetTimerManager().SetTimer(
            FlushTimerHandle, this,
            &URequirementWatcherComponent::FlushPendingEvaluations,
            FlushDelaySeconds, false);
    }
    // If timer already running, do nothing — let it fire at its scheduled time.
    // This bounds worst-case latency to FlushDelaySeconds regardless of event rate.
}

void URequirementWatcherComponent::FlushPendingEvaluations()
{
    FlushTimerHandle.Invalidate();

    // Move to local copy so dirty marks fired during evaluation queue for next flush.
    TSet<FRequirementSetHandle> ToEvaluate = MoveTemp(PendingDirtySet);

    FRequirementContext Ctx = BuildContext(); // constructs from owning PlayerState

    for (const FRequirementSetHandle& Handle : ToEvaluate)
    {
        FRequirementSetRuntime* Runtime = RegisteredSets.Find(Handle.Id);
        if (Runtime) EvaluateSetInternal(*Runtime);
    }
}
```

**Key properties:**

- `TSet` deduplicates: 20 `ItemAdded` events for the same quest set = 1 evaluation.
- Timer starts only on the **first** dirty mark. Subsequent marks in the same window do not reset the timer, bounding worst-case latency to `FlushDelaySeconds`.
- `MoveTemp` atomically clears the pending set before evaluation, so events fired *during* evaluation correctly queue for the next flush rather than being lost.

---

# Per-Requirement Cache During Evaluation

During `EvaluateSetInternal`, each requirement's result is checked against its cache entry before calling `Evaluate`:

```cpp
void URequirementWatcherComponent::EvaluateSetInternal(FRequirementSetRuntime& Runtime)
{
    TArray<URequirement*> AllRequirements = Runtime.Asset->GetAllRequirements();
    FRequirementContext Ctx = BuildContext();
    bool bAllPassed = true;

    for (int32 i = 0; i < AllRequirements.Num(); ++i)
    {
        URequirement* Req = AllRequirements[i];
        ERequirementCacheState& Cache = Runtime.CachedResults[i];

        // Monotonic requirement already permanently passed — skip evaluation entirely.
        if (Req->bIsMonotonic && Cache == ERequirementCacheState::CachedTrue)
            continue;

        FRequirementResult Result = Req->Evaluate(Ctx);
        Cache = Result.bPassed ? ERequirementCacheState::CachedTrue
                               : ERequirementCacheState::CachedFalse;

        if (!Result.bPassed)
        {
            bAllPassed = false;
            // For a list (AND-all) we can short-circuit here.
            // For an expression set, the set's Evaluate() handles short-circuiting internally.
            break;
        }
    }

    // Notify owning system via the registered callback.
    Runtime.OnDirty.ExecuteIfBound(Runtime.Handle, bAllPassed);
}
```

**Cache entries are parallel to `GetAllRequirements()`.** No `TMap` involved — lookup is array index O(1). Cache array is sized at `RegisterSet` time.

---

# Lifecycle and Initialization

**Component creation.** `URequirementWatcherComponent` is added to `APlayerState` at construction. `APlayerState` is the correct owner: it exists on both server and owning client, persists through pawn respawns, and is the canonical per-player data holder.

**Registration timing.** `RegisterSet` should be called after `APlayerState::BeginPlay` completes and all data components are initialized. For replicated systems, guard registration behind an `OnRep` or an explicit server-driven initialization RPC — not blindly in `BeginPlay`.

**Startup evaluation pass.** On the first `RegisterSet` call, the component performs a full synchronous evaluation of all requirements in the set to populate the cache. This is the only time a full pass runs; subsequent evaluations are dirty-only.

**Logout / PlayerState destruction.** `EndPlay` on the component calls `URequirementWatcherManager::UnregisterWatcher` and clears all registered sets. Owning systems do not need to manually unregister all handles on logout — the component cleanup handles it.

---

# Event Tag Conventions

All invalidation event tags live under the `RequirementEvent` root tag. Each module defines its own sub-namespace in `DefaultGameplayTags.ini`.

```
RequirementEvent
  ├── Leveling
  │     └── LevelChanged
  ├── Inventory
  │     ├── ItemAdded
  │     └── ItemRemoved
  ├── Quest
  │     ├── QuestCompleted
  │     └── QuestActivated
  ├── Reputation
  │     └── ReputationChanged
  └── Tag
        ├── TagAdded
        └── TagRemoved
```

**Rules:**

- Tags are defined in `DefaultGameplayTags.ini` in their owning module. Never in a central file.
- Use specific tags, not broad parent tags. `RequirementEvent.Inventory.ItemAdded` is correct. `RequirementEvent.Inventory` is too broad and will dirty sets that only care about removals.
- Tags are strings at authoring time — cache `FGameplayTag` handles at module startup via `UGameplayTagsManager::AddNativeGameplayTag` for zero-cost runtime lookup.

---

# Authority and Network Behaviour

The watcher component respects the `ERequirementEvalAuthority` set on each `FRequirementSetRuntime`:

| Authority | Server Behaviour | Client Behaviour |
| --- | --- | --- |
| `ServerOnly` | Evaluates on flush. Fires `OnDirty` callback. | Component exists but skips evaluation for these sets. |
| `ClientOnly` | Component exists but skips evaluation for these sets. | Evaluates on flush. Fires `OnDirty` callback locally. |
| `ClientValidated` | On receiving the validation RPC, re-evaluates fully from server context. Authoritative result sent back. | Evaluates on flush. On all-pass, fires Server RPC to trigger server re-evaluation. |

**`ClientValidated` RPC flow:**

```
Client flush → all requirements pass → Client fires Server_ValidateRequirementSet(Handle)
  → Server re-evaluates from server context
  → If pass: server takes the gated action (unlock quest, grant reward, etc.)
  → If fail: server discards and optionally sends ClientRPC with failure reason
```

The server never acts on the client's claimed result — only on its own re-evaluation.

---

# `FOnRequirementSetDirty`

Delegate fired by the watcher component when a set has been re-evaluated after being dirty. The owning system binds this at `RegisterSet` time.

```cpp
// Handle: identifies which registered set was evaluated.
// bAllPassed: combined pass/fail result of the set after re-evaluation.
DECLARE_DELEGATE_TwoParams(FOnRequirementSetDirty, FRequirementSetHandle, bool);
```

The owning system decides what to do with the result — update UI, unlock a quest, make an NPC visible, etc. The watcher component has no knowledge of game systems.

---

# Known Limitations

- **Flush delay adds latency.** Quest unlock or UI refresh may lag up to `FlushDelaySeconds` after the triggering event. This is intentional — tune per system. Reduce to 0.1s for UI-facing sets if needed.
- **`NotifyWorldEvent` is O(players watching tag).** Avoid for high-frequency events. World-scoped events like zone entry should be routed per-player where possible.
- **No cross-player aggregate requirements.** A set requiring "20 players in this zone" does not map to per-player watcher logic. Such aggregate conditions require a separate world-event component on a non-player actor. See main page for discussion.
- **Registration leaks if handle is not stored.** If the owning system discards the `FRequirementSetHandle`, it cannot unregister the set. The watcher component cleans up on `EndPlay`, but mid-session leaks waste subscription slots until logout.
- **Async requirements in watched sets.** Async requirements are not re-evaluated by the watcher flush — the flush calls `Evaluate()` (sync) only. If a set contains async requirements, the owning system must call `EvaluateSetNow` with the async path separately, or restructure so async requirements are one-shot checks outside the watcher.