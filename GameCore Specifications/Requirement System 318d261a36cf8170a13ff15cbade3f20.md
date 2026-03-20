# Requirement System

**Part of: GameCore Plugin** | **Status: Active Specification — v2** | **UE Version: 5.7**

The Requirement System is a stateless, data-driven condition evaluation layer. Any system — quests, interactions, abilities, crafting, dialogue — can express and evaluate prerequisites without coupling to other systems. Requirements are authored as Data Assets in the Unreal Details panel and evaluated on demand or reactively via the event bus.

---

# System Requirements

1. **Usable anywhere.** Requirements work on actors, subsystems, Data Assets, and non-actor contexts. No owning actor or component required.
2. **Stateless definitions.** A `URequirement` instance carries no per-player, per-evaluation, or per-frame state.
3. **No persistence in the requirement system.** Tracking accumulation belongs in the system that owns that data.
4. **No caching in the requirement system.** Evaluation is cheap. Systems that need to avoid re-evaluating cache the result themselves.
5. **Zero dependencies at the base layer.** `Requirements/` compiles with no imports from other GameCore modules.
6. **Boolean completeness.** Any AND/OR/NOT combination is expressible without custom C++ evaluation logic.
7. **Two evaluation paths.** Imperative: caller builds context and calls `Evaluate`. Reactive: `UGameCoreEventWatcher` + `URequirementWatchHelper` feed event payloads to callbacks.
8. **Server-authoritative.** Gameplay-gating evaluations happen server-side.

---

# Key Design Decisions

**Requirements are not trackers.** "Kill 10 wolves" is a stateful goal. Requirements only test true/false conditions derivable from current world state or an event payload.

**No persistence, no cache.** Both require an owner. Requirements are used on Data Assets with no persistent storage. Cache the `FRequirementResult` locally in the consuming system if needed.

**`FRequirementContext` wraps `FInstancedStruct`.** No typed fields. Any struct can be the context. Requirements cast `Context.Data` to their expected type. The event bus uses `FInstancedStruct` natively — event payloads pass through without translation.

**Two evaluate signatures.** `Evaluate(FRequirementContext)` for imperative snapshot checks. `EvaluateFromEvent(FRequirementContext)` for reactive event-driven checks. Default `EvaluateFromEvent` delegates to `Evaluate`.

**The requirement system does not own a watcher subsystem.** `URequirementWatcherManager` has been removed. Reactive evaluation is handled by `UGameCoreEventWatcher` (generic, owned by the Event Bus system) with `URequirementWatchHelper` providing a thin registration convenience layer.

**Closures carry caller context.** When a consuming system registers a watched list, it supplies a `TFunction<void(bool)>` that captures its own private data (quest ID, etc.). The helper and watcher never see that data. This solves the "how does the callback know which quest was unlocked" problem cleanly.

**Authority lives on the asset.** `ERequirementEvalAuthority` on `URequirementList`, enforced by `URequirementWatchHelper::PassesAuthority`. Call sites never override it.

---

# Architecture Overview

```
Consuming System (QuestComponent, InteractionComponent, ...)
  │
  │  1. Calls URequirementWatchHelper::RegisterList(
  │          List, [WeakThis, QuestId](bool bPassed) { ... })
  │
  │  2. Stores FEventWatchHandle
  │
  ▼
URequirementWatchHelper  (static utility)
  │  Collects watched tags from List->CollectWatchedEvents()
  │  Builds closure: event → EvaluateFromEvent → OnResult if changed
  │
  ▼
UGameCoreEventWatcher  (UWorldSubsystem)
  │  Registers closure per leaf tag
  │  Subscribes to UGameCoreEventBus lazily (one handle per tag)
  │
  ▼
UGameCoreEventBus  (UWorldSubsystem)
  │  Receives Broadcast() from any system
  │  Delivers FInstancedStruct payload synchronously
  │
  ▼
UGameCoreEventWatcher  dispatches to all registered closures for that tag
  │
  ▼
URequirementWatchHelper closure
  │  Wraps payload in FRequirementContext
  │  Calls URequirementList::EvaluateFromEvent(Context)
  │  If pass/fail changed → calls consuming system's OnResult(bPassed)
  │
  ▼
Consuming system OnResult closure
  Uses captured QuestId / ObjectiveId / etc. to act on the result
```

---

# Module Map

| Class / Type | File | Role |
|---|---|---|
| `URequirement` | `Requirements/Requirement.h/.cpp` | Abstract base. Stateless evaluation. |
| `URequirement_Composite` | `Requirements/RequirementComposite.h/.cpp` | AND/OR/NOT boolean tree. |
| `URequirementList` | `Requirements/RequirementList.h/.cpp` | Asset. Operator, authority, `EvaluateFromEvent`. |
| `URequirementWatchHelper` | `Requirements/RequirementWatchHelper.h/.cpp` | Static helper. Registers closures with `UGameCoreEventWatcher`. |
| `URequirementLibrary` | `Requirements/RequirementLibrary.h/.cpp` | Internal. `EvaluateAll`, `ValidateRequirements`. |
| `FRequirementContext` | `Requirements/RequirementContext.h` | Evaluation input. Wraps `FInstancedStruct`. |
| `FRequirementResult` | `Requirements/RequirementContext.h` | Pass/fail + reason. |
| `ERequirementEvalAuthority` | `Requirements/RequirementList.h` | ServerOnly / ClientOnly / ClientValidated. |
| `ERequirementListOperator` | `Requirements/RequirementList.h` | AND / OR. |
| `UGameCoreEventWatcher` | `EventBus/GameCoreEventWatcher.h/.cpp` | Generic event routing. Owned by Event Bus system. |

---

# File Structure

```
GameCore/Source/GameCore/
├── Requirements/
│   ├── Requirement.h / .cpp
│   ├── RequirementComposite.h / .cpp
│   ├── RequirementContext.h
│   ├── RequirementList.h / .cpp
│   ├── RequirementLibrary.h / .cpp
│   └── RequirementWatchHelper.h / .cpp    ← replaces RequirementWatcher
├── EventBus/
│   ├── GameCoreEventBus.h / .cpp
│   └── GameCoreEventWatcher.h / .cpp      ← generic, owned by event bus
├── Quest/Requirements/
├── Tags/Requirements/
└── Leveling/Requirements/
```

---

# Quick Usage Guide

## Imperative one-shot check

```cpp
FMyLevelContext LevelCtx;
LevelCtx.PlayerState = GetPlayerState();

FRequirementContext Ctx = FRequirementContext::Make(LevelCtx);
FRequirementResult Result = MyList->Evaluate(Ctx);
if (!Result.bPassed)
    ShowFailureReason(Result.FailureReason);
```

## Reactive watched evaluation

```cpp
// Registration — caller captures its own context in the closure.
TWeakObjectPtr<UMySystem> WeakThis = this;
FMyKey Key = MyKey;

WatchHandle = URequirementWatchHelper::RegisterList(this, MyList,
    [WeakThis, Key](bool bPassed)
    {
        if (UMySystem* Self = WeakThis.Get())
            Self->OnRequirementResult(Key, bPassed);
    });

// Teardown.
URequirementWatchHelper::UnregisterList(this, WatchHandle);
```

## Firing an event that requirements watch

```cpp
// In the leveling system, after a level-up:
FLevelChangedEvent Payload;
Payload.PlayerState = PS;
Payload.NewLevel    = NewLevel;

UGameCoreEventBus::Get(this)->Broadcast(
    FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"),
    FInstancedStruct::Make(Payload),
    EGameCoreEventScope::ServerOnly);
// No call to any watcher or requirement system needed.
```

---

# Network Considerations

| Concern | Approach |
|---|---|
| Authority enforcement | `URequirementWatchHelper::PassesAuthority` checks net role against `List->Authority` before evaluating. |
| Imperative server checks | Always build context server-side from the RPC connection. |
| `ClientValidated` | Client evaluates for responsiveness. On pass, fires Server RPC. Server re-evaluates from scratch. |
| `ClientOnly` | UI/cosmetic only. Server never evaluates. |
| Failure feedback | Consuming system sends targeted ClientRPC with `FRequirementResult.FailureReason`. |

---

# Sub-Pages

| Page | Covers |
|---|---|
| [Supporting Types](Requirement%20System/Supporting%20Types.md) | `FRequirementContext`, `FRequirementResult`, enums |
| [URequirement — Base Class](Requirement%20System/URequirement%20%E2%80%94%20Base%20Class%20319d261a36cf815f988bc5cacacd5ad0.md) | Full class, both evaluate paths, implementation guide |
| [URequirement_Composite](Requirement%20System/URequirement_Composite%20319d261a36cf81bf84aadce23da6e5a0.md) | AND/OR/NOT logic, authoring patterns |
| [URequirementList](Requirement%20System/Requirement%20Sets%2031dd261a36cf8167b97dc63857db467d.md) | Asset definition, operator, authority, `EvaluateFromEvent` |
| [Requirement Watch Helper](Requirement%20System/Watcher%20System%2031dd261a36cf81dab4b9e7ce3e690bde.md) | `URequirementWatchHelper`, registration pattern, authority, examples |
| [URequirementLibrary](Requirement%20System/URequirementLibrary%20319d261a36cf811cab04cd92452e80a3.md) | `EvaluateAll`, `ValidateRequirements` |
| [Design Decisions](Requirement%20System/Design%20Decisions.md) | Full history, rejected approaches, rationale |
