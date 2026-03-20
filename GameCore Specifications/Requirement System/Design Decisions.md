# Design Decisions

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

Full history of requirements, design evolution, and the rationale behind every significant decision. Read this before proposing changes to the system.

---

# System Requirements (Non-Negotiable)

1. Usable on Data Assets and non-actor contexts — no owning actor or component required.
2. `URequirement` instances carry zero per-player or per-evaluation state.
3. No persistence, no cache in the requirement system itself.
4. `Requirements/` module compiles with zero outgoing module dependencies.
5. Any AND/OR/NOT combination expressible without custom C++ evaluation logic.
6. Two evaluation paths: imperative (caller builds context) and reactive (event bus feeds context).
7. Server-authoritative for gameplay gates.

---

# Evolution History

## v1 — Gate Check (initial)

Started as `URequirement::Evaluate(Context) → bool`. Flat array of requirements evaluated with `EvaluateAll`. No failure reason, no OR/NOT logic, no async, no reactive evaluation.

## v1.1 — FRequirementResult, Sets, Composite

`FRequirementResult` added for failure reasons. `URequirementList` introduced as a `UPrimaryDataAsset` with AND/OR operator. `URequirement_Composite` added for NOT and nested logic. `ERequirementEvalAuthority` placed on the asset.

`URequirementSet` abstract base briefly specced as a shared base for future list types. Removed before implementation — YAGNI. `URequirementList` inherits `UPrimaryDataAsset` directly.

## v1.2 — Watcher System

`URequirementWatcherComponent` on `APlayerState` and `URequirementWatcherManager` (WorldSubsystem). Requirements declared invalidation tags via `GetWatchedEvents`. Manager routed events to components; components maintained per-player set registrations and a coalescing flush timer. `FRequirementSetRuntime` held per-player cache per list.

## v1.3 — Persisted Data / Payload

Attempt to handle tracker data (kill counts, delivery counts) inside the requirement system. `FRequirementPayload` (keyed counters/floats) injected into `FRequirementContext::PersistedData`. `URequirement_Persisted` sealed `Evaluate` and routed to `EvaluateWithPayload`. `TObjectPtr<UQuestComponent>` briefly added to `FRequirementContext` as a fast-path cache — removed immediately as it violated the zero-dependency rule.

## v2 — Current (Refactor)

Full redesign. All persistence and caching removed from the requirement system. `FRequirementContext` simplified to carry a single `FInstancedStruct Data`. Two evaluate paths (`Evaluate` / `EvaluateFromEvent`). `URequirementList` owns its reactive registration and `OnResultChanged` delegate directly. `URequirementWatcherManager` refactored to be a pure event bus bridge with no per-player state. All v1.x types that no longer exist are listed below.

---

# Key Decisions

## Requirements are not trackers

"Kill 10 wolves" is a goal with accumulating state. That belongs in a goal/objective system that owns counters, persistence, and progress. Requirements only answer true/false questions about current state. Conflating the two (v1.3 attempt) produced `FRequirementPayload`, `URequirement_Persisted`, and payload injection complexity that violated the statelessness contract and created coupling between `Requirements/` and the quest module.

## No persistence, no cache in the requirement system

Both require an owner. Requirements are used on Data Assets and non-actor contexts that have no persistent storage. Requirement evaluation (a component lookup and integer comparison) is cheap. Systems that need to avoid repeated evaluation cache the `FRequirementResult` themselves with one local bool. This is the right layer for that decision.

## `FInstancedStruct` as the universal context carrier

The old `FRequirementContext` had typed fields (`PlayerState`, `World`, `Instigator`, `PersistedData`). Every new requirement type that needed different data created pressure to add more fields, violating the zero-dependency rule (the `QuestComponent` field incident). `FInstancedStruct` carries any struct type without `Requirements/` importing it. The requirement subclass declares what it expects and casts. This is also the event bus mechanism, so event payloads pass through without translation.

## `FRequirementContext` as the named wrapper

`FInstancedStruct` alone is opaque at call sites. `FRequirementContext` is a named struct that makes intent clear — it is the evaluation input — while containing only `FInstancedStruct Data`. Adding typed fields directly to `FRequirementContext` is prohibited. All domain data lives inside `Data`.

## Two evaluate paths, not one

`Evaluate(FRequirementContext)` for imperative snapshot checks. `EvaluateFromEvent(FRequirementContext)` for reactive event-driven checks. The default implementation of `EvaluateFromEvent` delegates to `Evaluate`, so requirements that behave identically in both cases implement only `Evaluate`. Requirements that need event-specific behaviour (checking delta values, validating event source) override `EvaluateFromEvent` separately.

## `URequirementList` owns its reactive registration

In v1.x the consuming system created `FRequirementSetRuntime`, managed its lifecycle, and held the handle. This exposed internal requirement system types to consuming systems and required them to manage registration lifecycle. In v2 the list is the registration unit — it calls `Register(World)` / `Unregister(World)` itself. The consuming system only binds `OnResultChanged` and calls `Register`. No internal types leak.

## No per-player routing in the watcher

In v1.x the watcher manager routed events to per-player `URequirementWatcherComponent` instances. In v2 there is no per-player component. Requirements that need to filter by player identity do so inside `EvaluateFromEvent` by inspecting the event payload (e.g. checking `Payload.PlayerState == ExpectedPlayer`). This keeps the manager stateless with respect to players.

## Coalescing is last-write-wins per tag per frame

Multiple events with the same tag in one frame (20 item pickups) produce one evaluation per list per frame. The last payload wins. This is correct because requirements evaluate current state — intermediate states are irrelevant. Requirements that need to process every individual event (e.g. counting discrete occurrences) should not use the watcher; the owning system should call `Evaluate` directly.

## Authority lives on the asset

Call sites never pass or override authority. The watcher manager enforces it. If two systems need different authority for logically identical conditions, they reference two separate assets. This prevents authority bypass bugs at the programmer level.

---

# Removed Types (v1.x → v2)

| Removed | Reason |
|---|---|
| `FRequirementContext` typed fields (`PlayerState`, `World`, `Instigator`) | Replaced by `FInstancedStruct Data` |
| `FRequirementPayload` | Tracker data does not belong in requirements |
| `FRequirementSetRuntime` | List owns its own registration state |
| `FRequirementSetHandle` | No longer needed — list is the registration unit |
| `URequirementWatcherComponent` | No per-player component; manager is stateless re: players |
| `URequirement_Persisted` | Persistence belongs in goal/tracker systems |
| `ERequirementDataAuthority` | Removed with `URequirement_Persisted` |
| `ERequirementCacheState` | No cache in the system |
| `URequirementSet` abstract base | Was premature abstraction; never had >1 concrete type |
| `EEvaluateAsyncMode` | Async evaluation removed; all evaluation is synchronous |
| `MakeGuardedCallback` | With async removed, no longer needed |

---

# Open Issues

| Issue | Severity | Notes |
|---|---|---|
| Last-write-wins coalescing drops intermediate event payloads | Medium | Acceptable for state-query requirements. Systems needing per-event processing must call `Evaluate` directly. |
| No per-consumer `OnResultChanged` scoping | Low | Multiple bindings on the same list share pass/fail state. Acceptable since they observe the same gate. |
| Initial evaluation with empty context | Low | `Register()` calls `Evaluate` with an empty context to establish baseline. Event-only requirements return Fail as baseline, which is correct. |
