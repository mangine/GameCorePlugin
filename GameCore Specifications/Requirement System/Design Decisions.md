# Design Decisions

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

Full history of requirements, design evolution, and the rationale behind every significant decision.

---

# System Requirements (Non-Negotiable)

1. Usable on Data Assets and non-actor contexts — no owning actor or component required.
2. `URequirement` instances carry zero per-player or per-evaluation state.
3. No persistence, no cache in the requirement system.
4. `Requirements/` compiles with zero outgoing module dependencies.
5. Any AND/OR/NOT combination expressible without custom C++ evaluation logic.
6. Two evaluation paths: imperative (caller builds context) and reactive (event-driven).
7. Server-authoritative for gameplay gates.

---

# Evolution History

## v1 — Gate Check
`URequirement::Evaluate(Context) → bool`. Flat array, `EvaluateAll`. No failure reason, no OR/NOT, no reactive evaluation.

## v1.1 — FRequirementResult, Sets, Composite
`FRequirementResult` for failure reasons. `URequirementList` as `UPrimaryDataAsset` with AND/OR operator and authority. `URequirement_Composite` for NOT and nesting. `URequirementSet` abstract base briefly specced, removed before implementation — YAGNI.

## v1.2 — Watcher System
`URequirementWatcherComponent` on `APlayerState` and `URequirementWatcherManager` (WorldSubsystem). Per-player set registrations, coalescing flush timer, `FRequirementSetRuntime` cache per list per player.

## v1.3 — Persisted Data / Payload
Attempt to handle tracker data inside the requirement system. `FRequirementPayload`, `URequirement_Persisted`, `TObjectPtr<UQuestComponent>` briefly added to `FRequirementContext`. All removed — violated statelessness, zero-dependency, and separation-of-concerns rules.

## v2 — Refactor
All persistence and caching removed. `FRequirementContext` simplified to `FInstancedStruct Data`. Two evaluate paths. `URequirementWatcherManager` replaced by `UGameCoreEventWatcher` (generic) + `URequirementWatchHelper` (thin requirement-specific registration layer). See below.

## v2.1 — URequirementWatcherManager removed
`URequirementWatcherManager` was a WorldSubsystem that duplicated event routing logic. It had a critical bug: it subscribed to the parent tag `RequirementEvent` at Initialize time, which never fires due to GMS exact-match semantics. Additionally, its coalescing (last-write-wins per tag per frame) dropped intermediate payloads. Both issues were design flaws, not implementation details.

Replaced with:
- `UGameCoreEventWatcher` — generic routing subsystem owned by the Event Bus system. No domain knowledge. Lazy per-leaf-tag bus subscription. Immediate dispatch, no coalescing. Re-entrant safe.
- `URequirementWatchHelper` — static helper. Collects watched tags, builds evaluation closure, registers with `UGameCoreEventWatcher`. Returns one `FEventWatchHandle`.

---

# Key Decisions

## Requirements are not trackers
"Kill 10 wolves" is a goal. Requirements test facts. Conflating them (v1.3) produced payload injection complexity that violated statelessness and the zero-dependency rule.

## No persistence, no cache
Require an owner. Requirements are usable without one. Evaluation is cheap. Systems cache results themselves when needed.

## `FInstancedStruct` as the universal context carrier
Old `FRequirementContext` had typed fields that created import pressure. `FInstancedStruct` carries any struct type without `Requirements/` importing it. Event payloads arrive in this format from the bus — no translation needed.

## `FRequirementContext` as a named wrapper
Makes intent clear at call sites while containing only `FInstancedStruct Data`. Adding typed fields is prohibited.

## Two evaluate paths
`Evaluate` for imperative. `EvaluateFromEvent` for reactive. Default `EvaluateFromEvent` delegates to `Evaluate`. Override only when event-specific behaviour differs from snapshot behaviour.

## Closures carry caller context
The "how does the callback know which quest was unlocked" problem: the consuming system captures its own private data (quest ID, etc.) in the `OnResult` lambda at registration time. The helper stores `TFunction<void(bool)>`. No private context ever crosses into the requirement or watcher system. This is the standard partial application pattern applied consistently.

## Generic watcher, specific helper
The routing infrastructure (`UGameCoreEventWatcher`) is generic and reusable by any system — objective trackers, dialogue triggers, UI systems. The requirement-specific behaviour (evaluation, authority, change detection) lives in the helper. Systems that don't use requirements can use the watcher directly without importing `Requirements/`.

## No coalescing in the watcher
Coalescing (buffering events per frame) dropped intermediate payloads and added latency. Requirements evaluate current state — they don't accumulate. Immediate dispatch is correct. Systems that genuinely need coalescing own that logic.

## Authority lives on the asset
Call sites never pass or override authority. `URequirementWatchHelper::PassesAuthority` reads `List->Authority` and checks net role. Two assets needed for different authority on identical conditions — prevents authority bypass bugs.

---

# Removed Types (cumulative)

| Removed | Reason |
|---|---|
| `FRequirementContext` typed fields | Replaced by `FInstancedStruct Data` |
| `FRequirementPayload` | Tracker data belongs in owning system |
| `FRequirementSetRuntime` | No per-player cache |
| `FRequirementSetHandle` | Replaced by `FEventWatchHandle` from event watcher |
| `URequirementWatcherComponent` | No per-player component |
| `URequirementWatcherManager` | Replaced by generic `UGameCoreEventWatcher` |
| `URequirement_Persisted` | Persistence belongs in goal/tracker systems |
| `ERequirementDataAuthority` | Removed with `URequirement_Persisted` |
| `ERequirementCacheState` | No cache |
| `URequirementSet` abstract base | Premature abstraction |
| `EEvaluateAsyncMode` | Async evaluation removed |
| `MakeGuardedCallback` | With async removed, not needed |

---

# Open Issues

| Issue | Severity | Notes |
|---|---|---|
| No parent tag subscription | Medium | GMS exact-match only. Leaf tags must be registered individually. If wildcard subscription is needed, `UGameCoreEventWatcher::RegisterHierarchy` could wrap `UGameplayTagsManager::RequestGameplayTagChildren`. |
| Initial baseline evaluation | Low | `URequirementWatchHelper` has no built-in initial evaluation. Consuming systems that need an immediate baseline should call `List->Evaluate(Ctx)` imperatively at setup time. |
| Mixed imperative+event lists | Low | A list with event-only requirements returns wrong results when called imperatively. Document clearly in the Data Asset which evaluation path the list supports. |
