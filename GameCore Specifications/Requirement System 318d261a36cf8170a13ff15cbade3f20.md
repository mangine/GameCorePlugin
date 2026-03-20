# Requirement System

**Part of: GameCore Plugin** | **Status: Active Specification — v2** | **UE Version: 5.7**

The Requirement System is a stateless, data-driven condition evaluation layer. Any system — quests, interactions, abilities, crafting, dialogue — can express and evaluate prerequisites without coupling to other systems. Requirements are authored as Data Assets in the Unreal Details panel and evaluated on demand or reactively via the event bus.

---

# System Requirements

These are the non-negotiable constraints that drove every design decision:

1. **Usable anywhere.** Requirements must work on actors, subsystems, Data Assets, and non-actor contexts equally. No owning actor or component is required to use the system.
2. **Stateless definitions.** A `URequirement` instance authored in a Data Asset carries no per-player, per-evaluation, or per-frame state. The same object is evaluated against many different contexts.
3. **No persistence in the requirement system.** Tracking accumulation (kill counts, delivery counts) is the responsibility of the system that owns that data — not requirements. Requirements only test conditions that are true or false right now.
4. **No caching in the requirement system.** No cache means no storage, no lifecycle, no owner. Evaluation is cheap enough that caching is not worth the complexity.
5. **Zero dependencies at the base layer.** `Requirements/` compiles with no imports from other GameCore modules.
6. **Boolean completeness.** Any AND/OR/NOT combination is expressible without writing custom C++ evaluation logic.
7. **Two evaluation paths.** Imperative: caller builds a context struct and calls `Evaluate` directly. Reactive: the watcher manager feeds event payloads to subscribed requirement lists automatically.
8. **Server-authoritative.** Gameplay-gating evaluations happen server-side. Client evaluation is for display only.

---

# Key Design Decisions

**Requirements are not trackers.** "Kill 10 wolves" is a goal with state that accumulates over time. That belongs in a goal/objective system. "Has killed at least 10 wolves" is a requirement — a true/false question answered from current world state or an event payload. If a system needs to track progress, it tracks it externally and exposes a fact that requirements can test.

**No persistence, no cache.** Both require an owner. Requirements are used on Data Assets and non-actor contexts that have no persistent storage. The evaluation cost of a requirement (a component lookup and comparison) is far lower than the complexity cost of managing a cache. Systems that genuinely need to avoid re-evaluating can cache the `FRequirementResult` themselves with one local bool.

**`FRequirementContext` replaces all previous context types.** The old `FRequirementContext` (with typed fields), `FRequirementPayload`, and `FRequirementSetRuntime` are removed. The new `FRequirementContext` carries a single `FInstancedStruct Data` field — the caller puts whatever struct the requirement expects into it. Requirements cast it to their expected type. This is the same mechanism the event bus uses, so watcher-delivered event payloads can be passed directly.

**Two evaluate signatures, not one.** `Evaluate(FRequirementContext)` is for imperative checks — the caller builds context explicitly. `EvaluateFromEvent(FRequirementContext)` is for reactive checks — the watcher manager wraps the event payload and calls this. Requirements may implement one or both. The distinction allows requirements to behave differently on a snapshot query vs a live event (e.g. validate the event source, check delta values).

**`URequirementList` subscribes to the watcher, not the other way around.** A list registers its watched event tags with `URequirementWatcherManager`. When a matching event arrives, the manager calls `NotifyEvent` on the list. The list evaluates itself and fires its `OnResultChanged` delegate if the pass/fail result changed. The consuming system never touches the evaluation loop — it only binds to the delegate.

**Authority lives on the asset.** `ERequirementEvalAuthority` is a property of `URequirementList`, set by the designer. Call sites never pass or override it. The watcher manager enforces it — a `ServerOnly` list never evaluates on the client.

**`URequirementSet` abstract base was removed.** It was premature abstraction for one concrete type. Reintroduce only if a second list type materialises.

**`URequirement_Persisted`, `FRequirementPayload`, `FRequirementSetRuntime`, `FRequirementSetHandle`, `URequirementWatcherComponent`, `ERequirementDataAuthority`** — all removed. Their responsibilities either belong in consuming systems or were unnecessary complexity.

---

# Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                   Consuming System                       │
│  (QuestComponent, InteractionComponent, AbilitySystem)   │
│                                                          │
│  ┌──────────────────┐    binds delegate                 │
│  │ URequirementList │◄────────────────────────────────  │
│  │  (Data Asset)    │    OnResultChanged(bool bPassed)  │
└──┴────────┬─────────┴──────────────────────────────────-┘
            │ subscribes watched tags at Register()
            ▼
┌───────────────────────────────┐
│  URequirementWatcherManager   │  UWorldSubsystem
│  TMap<FGameplayTag,           │
│    TArray<WeakPtr<List>>>     │◄── event bus delivers FInstancedStruct
└───────────────────────────────┘
            │ NotifyEvent(tag, FRequirementContext)
            ▼
┌─────────────────────────────────┐
│       URequirementList          │
│  Operator (AND/OR)              │
│  Authority                      │
│  TArray<URequirement*>          │
│                                 │
│  EvaluateAll(Context)           │
│    → URequirementLibrary        │
│  fires OnResultChanged if       │
│  pass/fail state changed        │
└─────────────────────────────────┘
            │ Evaluate(FRequirementContext)
            ▼
┌───────────────────────────────────┐
│         URequirement              │
│  (stateless, authored in asset)   │
│  Evaluate(FRequirementContext)    │
│  EvaluateFromEvent(FRequirementContext) │
│  GetWatchedEvents()               │
└───────────────────────────────────┘
```

---

# Module Map

| Class / Type | File | Role |
|---|---|---|
| `URequirement` | `Requirements/Requirement.h/.cpp` | Abstract base. Stateless evaluation contract. |
| `URequirement_Composite` | `Requirements/RequirementComposite.h/.cpp` | AND/OR/NOT boolean tree. |
| `URequirementList` | `Requirements/RequirementList.h/.cpp` | Asset grouping requirements. Operator, authority, delegate. |
| `URequirementWatcherManager` | `Requirements/RequirementWatcher.h/.cpp` | WorldSubsystem. Event bus bridge. Routes events to lists. |
| `URequirementLibrary` | `Requirements/RequirementLibrary.h/.cpp` | Internal helper. EvaluateAll, ValidateRequirements. |
| `FRequirementContext` | `Requirements/RequirementContext.h` | Evaluation input. Wraps `FInstancedStruct`. |
| `FRequirementResult` | `Requirements/RequirementContext.h` | Evaluation output. Pass/fail + reason. |
| `ERequirementEvalAuthority` | `Requirements/RequirementList.h` | ServerOnly / ClientOnly / ClientValidated. |
| `ERequirementListOperator` | `Requirements/RequirementList.h` | AND / OR. |

---

# File Structure

```
GameCore/Source/GameCore/
├── Requirements/                        ← zero outgoing module dependencies
│   ├── Requirement.h / .cpp
│   ├── RequirementComposite.h / .cpp
│   ├── RequirementContext.h
│   ├── RequirementList.h / .cpp
│   ├── RequirementLibrary.h / .cpp
│   └── RequirementWatcher.h / .cpp
│
├── Quest/Requirements/
│   ├── Requirement_QuestCompleted.h/.cpp
│   └── Requirement_ActiveQuestCount.h/.cpp
├── Tags/Requirements/
│   └── Requirement_HasTag.h/.cpp
└── Leveling/Requirements/
    └── Requirement_MinLevel.h/.cpp
```

---

# Quick Usage Guide

## Imperative one-shot check

```cpp
// Build a context containing whatever data the requirements need.
FMyLevelContext LevelCtx;
LevelCtx.PlayerState = GetPlayerState();

FRequirementContext Ctx;
Ctx.Data.InitializeAs<FMyLevelContext>(LevelCtx);

FRequirementResult Result = MyList->Evaluate(Ctx);
if (!Result.bPassed)
    ShowFailureReason(Result.FailureReason);
```

## Reactive watched evaluation

```cpp
// At setup — bind and register.
MyList->OnResultChanged.BindUObject(this, &UMySystem::OnRequirementsChanged);
MyList->Register(GetWorld()); // subscribes to watcher manager

// Callback fires when pass/fail state changes.
void UMySystem::OnRequirementsChanged(bool bPassed)
{
    bRequirementsMet = bPassed;
}

// At teardown.
MyList->Unregister(GetWorld());
MyList->OnResultChanged.Unbind();
```

## Firing an event that requirements watch

```cpp
// Any system fires this when relevant state changes.
FPlayerLevelChangedEvent Payload;
Payload.NewLevel = 15;

URequirementWatcherManager* Mgr =
    GetWorld()->GetSubsystem<URequirementWatcherManager>();
Mgr->BroadcastEvent(
    FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"),
    FInstancedStruct::Make(Payload));
```

---

# Network Considerations

| Concern | Approach |
|---|---|
| Authority enforcement | `URequirementWatcherManager` skips evaluation for lists whose authority doesn't match the current net role. |
| Imperative server checks | Always build context server-side from the RPC connection. Never trust client-provided data. |
| `ClientValidated` | Client evaluates for responsiveness. On pass, fires Server RPC. Server re-evaluates from scratch. |
| `ClientOnly` | Purely cosmetic / UI. Server never evaluates. |
| Failure feedback | Consuming system sends a targeted ClientRPC with `FRequirementResult.FailureReason`. |

---

# Sub-Pages

| Page | Covers |
|---|---|
| [Supporting Types](Requirement%20System/Supporting%20Types.md) | `FRequirementContext`, `FRequirementResult`, all enums |
| [URequirement — Base Class](Requirement%20System/URequirement%20%E2%80%94%20Base%20Class%20319d261a36cf815f988bc5cacacd5ad0.md) | Full class, both evaluate paths, implement guide, examples |
| [URequirement_Composite](Requirement%20System/URequirement_Composite%20319d261a36cf81bf84aadce23da6e5a0.md) | AND/OR/NOT logic, async, authoring patterns |
| [URequirementList](Requirement%20System/Requirement%20Sets%2031dd261a36cf8167b97dc63857db467d.md) | Asset definition, operator, authority, delegate, register/unregister |
| [URequirementWatcherManager](Requirement%20System/Watcher%20System%2031dd261a36cf81dab4b9e7ce3e690bde.md) | Event bus bridge, routing, authority enforcement, coalescing |
| [URequirementLibrary](Requirement%20System/URequirementLibrary%20319d261a36cf811cab04cd92452e80a3.md) | EvaluateAll, ValidateRequirements |
| [Design Decisions](Requirement%20System/Design%20Decisions.md) | Full history, rejected approaches, rationale |
