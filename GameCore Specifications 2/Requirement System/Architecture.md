# Requirement System — Architecture

**Part of:** GameCore Plugin | **Module:** `GameCoreRequirements` (compiled inside `GameCore`) | **UE Version:** 5.7

The Requirement System is a stateless, data-driven condition evaluation layer. Any system — quests, interactions, abilities, crafting, dialogue — can express and evaluate prerequisites without coupling to other systems. Requirements are authored as inline objects inside `URequirementList` Data Assets and evaluated on demand (imperative) or reactively via the Event Bus.

---

## Dependencies

### Unreal Engine Modules
| Module | Reason |
|---|---|
| `Engine` | `UPrimaryDataAsset`, `UBlueprintFunctionLibrary`, `UObject` |
| `StructUtils` | `FInstancedStruct` — universal context and event payload carrier |
| `GameplayTags` | `FGameplayTag`, `FGameplayTagContainer` — event routing keys |
| `GameplayAbilities` | Optional. Used only by concrete requirement subclasses that read from `UAbilitySystemComponent`. Not a base-layer dependency. |

### GameCore System Dependencies
| System | Dependency type | Reason |
|---|---|---|
| **Event Bus System** | Runtime optional | `UGameCoreEventWatcher` for reactive watch registration. `UGameCoreEventBus` for broadcasting invalidation events. The base `Requirements/` module compiles without this — only `RegisterWatch` needs it. |

> **Zero base-layer dependencies.** `Requirements/Requirement.h`, `RequirementComposite.h`, `RequirementContext.h` compile with no imports from other GameCore modules. Concrete subclasses in `Leveling/Requirements/`, `Tags/Requirements/`, etc., import their own module only.

---

## Requirements

1. Usable on actors, subsystems, Data Assets, and non-actor contexts. No owning actor or component required.
2. `URequirement` instances carry zero per-player, per-evaluation, or per-frame state.
3. No persistence and no cache in the requirement system. Consuming systems cache `FRequirementResult` if needed.
4. `Requirements/` compiles with zero outgoing module dependencies.
5. Any AND / OR / NOT boolean combination is expressible without custom C++ evaluation logic.
6. Two evaluation paths: imperative (caller builds context) and reactive (event-driven via `RegisterWatch`).
7. Server-authoritative for all gameplay-gating evaluations. Authority is declared on the asset.

---

## Features

- **Stateless leaf requirements** authored inline in a `URequirementList` Data Asset via the Details panel class picker.
- **Composite boolean logic** — AND / OR / NOT trees via `URequirement_Composite`, nestable arbitrarily.
- **Imperative evaluation** — `List->Evaluate(Context)` for one-shot synchronous checks.
- **Reactive evaluation** — `List->RegisterWatch(Owner, Closure)` subscribes to relevant events; the closure fires only when pass/fail state changes.
- **Authority enforcement** — `ERequirementEvalAuthority` (ServerOnly / ClientOnly / ClientValidated) on the asset; the watcher scope filter enforces it.
- **Localised failure reasons** — `FRequirementResult::FailureReason` is `FText`, surfaced to the client by the consuming system.
- **Dev-build validation** — `URequirementLibrary::ValidateRequirements` catches null entries, malformed NOT composites, and authority mismatches at `BeginPlay`.
- **Blueprint-authorable** — all requirement subclasses are `Blueprintable`; `GetDescription()` appears in the Details panel tooltip.

---

## Design Decisions

**Requirements are not trackers.** "Kill 10 wolves" is a stateful goal. Requirements only test true/false conditions derivable from current world state or an event payload. Accumulation belongs in the system that owns that data.

**No persistence, no cache.** Both require an owner. Requirements are used on Data Assets without one. Evaluation is cheap. Systems that need to avoid re-evaluating cache the result themselves.

**`FInstancedStruct` as the universal context carrier.** Old typed fields on `FRequirementContext` created import pressure in `Requirements/`. `FInstancedStruct` carries any struct without any include. Event payloads arrive in this format from the bus — no translation needed.

**Two evaluate signatures.** `Evaluate` for imperative snapshot checks. `EvaluateFromEvent` for reactive event-driven checks. Default `EvaluateFromEvent` delegates to `Evaluate` — override only when event-specific behaviour differs.

**`RegisterWatch` lives on `URequirementList`.** The registration logic belongs on the object being registered. Consuming systems find it directly on the list they already hold. A separate helper class is unnecessary and was removed.

**Closures carry caller context.** When registering a watched list, the caller supplies a `TFunction<void(bool)>` that captures its own private data (quest ID, etc.). The watcher never sees that data. This is the standard partial-application pattern.

**Authority lives on the asset.** `ERequirementEvalAuthority` on `URequirementList` is read by `RegisterWatch` and mapped to `EGameCoreEventScope`. Call sites never override it. Two assets are needed for different authority on identical conditions — prevents authority bypass.

**`URequirementLibrary` is internal.** It is NOT a `UBlueprintFunctionLibrary`. It is a plain C++ helper class (`static` methods) for the evaluation loop. Consuming systems call `List->Evaluate()` only.

---

## Logic Flow

### Imperative Path

```
Consuming System
  │  Builds FRequirementContext::Make<T>(SnapshotData)
  │  Calls List->Evaluate(Ctx)
  ▼
URequirementList::Evaluate
  │  Forwards to URequirementLibrary::EvaluateAll(Requirements, Operator, Ctx)
  ▼
URequirementLibrary::EvaluateAll
  │  Iterates Requirements[]
  │  Calls Req->Evaluate(Ctx) on each
  │  Short-circuits on AND failure / OR pass
  ▼
URequirement (leaf or composite)::Evaluate
  │  Casts Context.Data to expected struct
  │  Returns FRequirementResult { bPassed, FailureReason }
  ▼
Consuming System receives FRequirementResult
  │  On Fail: sends FailureReason to client via targeted RPC
  │  On Pass: executes the action
```

### Reactive Path

```
Consuming System calls:
  List->RegisterWatch(this, [WeakThis, MyKey](bool bPassed) { ... })
    │
    ├── CollectWatchedEvents() → FGameplayTagContainer
    ├── AuthorityToScope()     → EGameCoreEventScope
    └── UGameCoreEventWatcher::Register(Owner, Tags, Scope, Closure)
          └── Lazy bus subscription per leaf tag
          └── Returns FEventWatchHandle ← stored by consuming system

When a watched event fires via UGameCoreEventBus::Broadcast(...):
  UGameCoreEventWatcher receives delivery
  Checks scope → skips if net role does not match
  Calls registered closure:
    Wraps payload in FRequirementContext
    Calls List->EvaluateFromEvent(Ctx)
    Compares result to TSharedPtr<TOptional<bool>> LastResult
    If pass/fail changed → calls OnResult(bPassed)
      └── Consuming system closure fires with captured context

Teardown:
  List->UnregisterWatch(this, Handle)
```

---

## Known Issues

| Issue | Severity | Notes |
|---|---|---|
| No parent tag subscription | Medium | `UGameCoreEventWatcher` uses GMS exact-match semantics. Leaf tags must be registered individually. `RegisterHierarchy` could wrap `UGameplayTagsManager::RequestGameplayTagChildren` if wildcard subscription is ever needed. |
| No built-in initial baseline evaluation | Low | `RegisterWatch` only fires when an event arrives after registration. Consuming systems that need an immediate baseline must call `List->Evaluate(Ctx)` imperatively at setup time. |
| Mixed imperative + event-only lists | Low | A list containing event-only requirements (those that return Fail from `Evaluate`) produces incorrect results when called imperatively. Document at the Data Asset level which evaluation path the list supports. |
| `EvaluateFromEvent` shares context with imperative path in OR lists | Low | An event-only requirement in an OR list may cause incorrect OR evaluation if the event context struct is not the type the other requirements expect. Place event-only requirements in dedicated lists. |

---

## File Structure

```
GameCore/Source/GameCore/
├── Requirements/                           ← zero external GameCore dependencies
│   ├── RequirementContext.h                  FRequirementContext, FRequirementResult
│   ├── Requirement.h / .cpp                  URequirement abstract base
│   ├── RequirementComposite.h / .cpp         URequirement_Composite (AND/OR/NOT)
│   ├── RequirementList.h / .cpp              URequirementList (PrimaryDataAsset)
│   └── RequirementLibrary.h / .cpp           URequirementLibrary internal evaluation helper
├── EventBus/                               ← owned by Event Bus system
│   ├── GameCoreEventBus.h / .cpp
│   └── GameCoreEventWatcher.h / .cpp
│
│   ── Concrete requirement subclasses live in their owning modules ──
│
├── Leveling/Requirements/
│   └── Requirement_MinLevel.h
├── Tags/Requirements/
│   └── Requirement_HasTag.h
└── Quest/Requirements/
    └── Requirement_QuestCompleted.h
```

> **Rule:** Requirement subclasses live in the module that owns the data they query. The base `Requirements/` folder contains only abstract infrastructure.

---

## Class / Type Map

| Class / Type | File | Role |
|---|---|---|
| `FRequirementContext` | `Requirements/RequirementContext.h` | Evaluation input. Wraps `FInstancedStruct`. |
| `FRequirementResult` | `Requirements/RequirementContext.h` | Pass/fail + localised failure reason. |
| `ERequirementEvalAuthority` | `Requirements/RequirementList.h` | ServerOnly / ClientOnly / ClientValidated. |
| `ERequirementListOperator` | `Requirements/RequirementList.h` | AND / OR. |
| `URequirement` | `Requirements/Requirement.h/.cpp` | Abstract base. Stateless evaluation. |
| `URequirement_Composite` | `Requirements/RequirementComposite.h/.cpp` | AND / OR / NOT boolean tree. |
| `URequirementList` | `Requirements/RequirementList.h/.cpp` | Asset. Operator, authority, imperative + reactive evaluation. |
| `URequirementLibrary` | `Requirements/RequirementLibrary.h/.cpp` | Internal C++ helper. `EvaluateAll`, `ValidateRequirements`. |
| `UGameCoreEventWatcher` | `EventBus/GameCoreEventWatcher.h/.cpp` | Generic event routing. Owned by Event Bus system. |
