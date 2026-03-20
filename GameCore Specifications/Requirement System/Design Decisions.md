# Design Decisions

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

This document records the requirements, feature evolution, and key architectural decisions made for the Requirement System. It exists so future contributors understand not just *what* the system does but *why* specific choices were made and what alternatives were considered.

---

# System Requirements

The following were the non-negotiable requirements that drove the initial design:

1. **Reusable across all systems.** The same mechanism must serve quests, interactions, abilities, dialogue, crafting, and any future system without modification.
2. **Designer-owned.** Prerequisites must be configurable in the Unreal Details panel without C++ changes per feature.
3. **Server-authoritative.** Any gameplay-gating check must be evaluated server-side. Client evaluation is permitted only for display.
4. **Stateless definitions.** A requirement definition shared across 1,000 concurrent players must carry no per-player state.
5. **Decoupled at the base layer.** The `Requirements/` module must compile with zero dependencies on other GameCore modules.
6. **Boolean completeness.** Any logical combination of conditions (AND, OR, NOT, nested) must be expressible without writing custom C++.
7. **Reactive, not polling.** For systems that watch requirement availability over time, the cost must be O(sets watching event) per state change — never O(all requirements × all players).

---

# Evolution History

## Phase 1 — Gate Check

The system began as a simple gate: `URequirement::Evaluate(Context) → bool`. A consuming system held an array of `URequirement*` and called `EvaluateAll`. Results were not stored — every call re-evaluated from scratch. This was sufficient for one-shot interaction gates ("can the player interact with this chest?").

**Limitations identified:**
- No way to express OR or NOT logic in the array without writing bespoke C++.
- No way to reactively watch a gate — every system polled on a timer or re-checked on every relevant event.
- `bool` return meant no player-facing failure reason.

## Phase 2 — Requirement Sets + `FRequirementResult`

`URequirementList` was introduced as a `UPrimaryDataAsset` grouping requirements with an AND/OR operator and an authority declaration. `URequirement_Composite` was added to express NOT and nested boolean trees. `FRequirementResult` replaced `bool` to carry a `FailureReason`.

`URequirementSet` (an abstract base) was briefly specced as a base for multiple set types. It was removed before implementation — a second set type never materialised, and adding an abstract base for one concrete class was premature abstraction. `URequirementList` directly inherits `UPrimaryDataAsset`.

**Decision: authority lives on the asset, not at the call site.**
Authority (`ServerOnly`, `ClientOnly`, `ClientValidated`) is a property of the `URequirementList` asset set by the designer. Consuming systems never pass or override authority. If two systems need different authority for logically identical conditions, they reference two separate assets. This was chosen over a call-site parameter to prevent authority bypass bugs — a programmer cannot accidentally evaluate a `ServerOnly` list on the client by forgetting to pass the right enum.

## Phase 3 — Watcher System

The quest system required reactive tracking: "notify me when the player first becomes eligible for this quest." Polling was ruled out immediately given expected player counts (1,000+) and quest counts (500+).

The watcher system inverts control: requirements declare which `RequirementEvent.*` GameplayTags invalidate them. When a tag fires, only sets watching that tag are dirtied. A coalescing timer batches rapid state changes into one evaluation per set.

**Decision: tags over delegates for invalidation events.**
Each requirement declares its invalidation events as `FGameplayTag` values, not as bound delegates. Tags decouple modules — `URequirement_MinLevel` does not import the Leveling module. The leveling system fires `RequirementEvent.Leveling.LevelChanged` and the watcher routes it, with no shared type between them.

**Decision: coalescing timer, not immediate re-evaluation.**
When 20 item pickups happen in one frame, each fires `RequirementEvent.Inventory.ItemAdded`. Without coalescing, each event triggers a full requirement evaluation for every watching set. The timer deduplicates these into one flush per set, bounded by `FlushDelaySeconds`. Worst-case latency is the flush delay — intentional and tunable per system.

## Phase 4 — Persisted Data / Payload Injection

The quest tracker system needed requirements to evaluate against runtime counter data (kill counts, delivery counts) that is not derivable from world state. The counter values are owned by `UQuestComponent` and change frequently.

**Problem:** `URequirement` is stateless. It cannot cache anything. But the data it needs lives in a system (`UQuestComponent`) it must not import.

**Rejected approach: component pointer in context.**
Adding `TObjectPtr<UQuestComponent> QuestComponent` to `FRequirementContext` was implemented briefly but removed. It created a compile-time dependency from `Requirements/` → Quest module, violating the zero-dependency rule. It also established a precedent — the next system (Reputation, Inventory) would add its own pointer, turning `FRequirementContext` into a grab-bag.

**Chosen approach: payload injection via `PersistedData`.**
`FRequirementContext::PersistedData` is a `TMap<FGameplayTag, FRequirementPayload>`. The owning system constructs an `FRequirementPayload` with the relevant counters and floats, inserts it under a domain tag (e.g. the QuestId), and passes the context to `Evaluate`. The requirement looks up its domain tag, retrieves the payload, and reads the counter. Zero coupling in either direction.

See [Supporting Types — Why PersistedData is a TMap](Supporting%20Types.md#why-persisteddata-is-tmapfgameplaytag-frequirementpayload-and-not-just-frequirementpayload) for the full explanation.

`URequirement_Persisted` was added as an abstract base to seal the payload lookup pattern — subclasses implement `EvaluateWithPayload(Context, Payload)` and cannot accidentally bypass the domain tag lookup.

---

# Key Architectural Decisions

## `URequirement` is a `UObject`, not a struct or interface

Making requirements `UObject` subclasses enables the Unreal Details panel class picker with `EditInlineNew`, which is the primary authoring mechanism. An interface approach would require concrete implementations elsewhere, losing the inline authoring. A struct approach would lose polymorphism entirely. The `UObject` overhead is acceptable — requirement assets are loaded once and shared.

## Async evaluation is opt-in, not the default

Most requirements read replicated in-memory data — level, inventory, tags. Forcing async evaluation for these would add unnecessary complexity (callbacks, error handling, timeouts) to the common case. `IsAsync()` is opt-in. The library (`EvaluateAllAsync`) handles the mixed sync+async case transparently.

## `MakeGuardedCallback` is a protected helper, not a public API

Async requirement implementors have three systematic failure modes: no timeout, firing the callback twice, and capturing `this` raw. `MakeGuardedCallback` eliminates all three in one call. It is protected on `URequirement` so it is available to all implementors without being callable from outside.

## `URequirementLibrary` is internal, not a public API

Consuming systems call `List->Evaluate(Context)`. They do not call `URequirementLibrary::EvaluateAll` directly. This ensures that the OR/AND operator on the list is always respected and authority validation always runs. `URequirementLibrary` is an implementation detail of `URequirementList` — if the evaluation internals change, consuming systems are unaffected.

## `bIsMonotonic` is a property, not a class hierarchy

A monotonic requirement (one that can only ever go from Fail to Pass) could have been modelled as a separate subclass. Instead it is a `bool UPROPERTY` on `URequirement`. This keeps the class hierarchy flat — authors set it in the Details panel on any requirement instance without creating a new subclass.

## `EvaluateSetInternal` should evaluate through `List->Evaluate`, not the flat array

The watcher's `EvaluateSetInternal` iterates the flat requirement array directly, bypassing the `URequirementList` operator (AND vs OR). This is a known spec gap — the correct implementation calls `Runtime.Asset->Evaluate(Ctx)` and reconciles the per-requirement monotonic cache separately. The flat iteration was an early shortcut that was never fixed. **This must be corrected before production.** The per-requirement cache optimisation (skipping monotonic CachedTrue entries) should be implemented by `URequirementList::Evaluate` checking the cache before dispatching to each requirement, not by the watcher iterating the array itself.

## `URequirementSet` abstract base was removed

Briefly specced as a shared base for `URequirementList` and a potential future `URequirementTree` type. Removed before implementation — YAGNI. If a second set type is ever needed, introduce the base at that time with full knowledge of what both types share.

## No central requirement registry

Requirement types are discovered via Unreal's reflection system when their owning module loads. There is no `RegisterRequirementType()` call, no factory, and no central list. This keeps each module fully self-contained and eliminates a potential load-order dependency.

---

# Open Issues

| Issue | Severity | Notes |
|---|---|---|
| `EvaluateSetInternal` bypasses `URequirementList` operator | High | Must call `List->Evaluate` instead of iterating flat array. Monotonic cache must move into `URequirementList::Evaluate`. |
| Async requirements silently evaluate as Fail in watcher flush | High | Watcher flush calls `Evaluate()` (sync). Async requirements return `Fail` by default. `RegisterSet` should reject sets with async requirements, or the flush must route async requirements through `EvaluateAsync`. |
| `URequirement_Persisted` has no standalone sub-page | Low | Interim definition in Quest System's `GameCore Changes.md`. Promoted to `URequirement — Base Class` sub-page is the target. |
| `URequirementLibrary` doc still says `URequirementSet` in opening line | Low | Stale copy from before `URequirementSet` was removed. |
