# Requirement System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Requirement System is a data-driven, polymorphic condition evaluation layer. It provides a single, reusable mechanism for expressing and evaluating prerequisites — usable by any system (quests, interactions, abilities, dialogue, crafting) without coupling those systems together. Requirements are configured directly in Data Assets using instanced UObjects with designer-friendly properties. Evaluation is synchronous by default; an async path exists for conditions that depend on data not resident in memory.

---

# System Modules

| Module | Classes | Role |
|---|---|---|
| **Core Evaluation** | `URequirement`, `URequirement_Composite`, `URequirementLibrary` | Abstract base, boolean logic tree, evaluation entry point |
| **Payload Base** | `URequirement_Persisted` | Abstract base for requirements reading runtime counter/float data via context payload |
| **Requirement Sets** | `URequirementList` | Data Asset grouping requirements with AND/OR operator and authority declaration |
| **Watcher System** | `URequirementWatcherComponent`, `URequirementWatcherManager` | Event-driven per-player dirty tracking. Eliminates polling. |
| **Supporting Types** | `FRequirementContext`, `FRequirementPayload`, `FRequirementResult`, `FRequirementSetRuntime`, enums | Evaluation inputs, outputs, payload injection, per-player runtime cache |

---

# Core Design Principles

- **Requirements are definitions, not instances.** `URequirement` objects are authored in Data Assets and loaded once. They carry no per-player state. The same object is evaluated against many players using only `FRequirementContext`.
- **Synchronous by default, async by exception.** `Evaluate` must return immediately for the vast majority of requirements. `EvaluateAsync` exists only when data is genuinely not resident in memory.
- **Authority lives on the server.** Systems that gate gameplay actions must evaluate server-side. Client evaluation is permitted only for display and UI gating.
- **No cross-system imports at the base layer.** `Requirements/` has zero outgoing module dependencies. `FRequirementContext` never includes a typed pointer to any class outside `Requirements/`, `Engine`, or `GameplayTags`.
- **Events are GameplayTags, not enums.** Each module registers its own invalidation tags under `RequirementEvent.*`. Zero coupling between modules.
- **Composites replace hardcoded logic.** AND/OR/NOT expressions use `URequirement_Composite` trees — not bespoke C++ evaluators.
- **Watcher is push-invalidated, pull-evaluated.** Requirements never poll. Relevant events dirty sets; evaluation runs on the next throttled flush.
- **Payload injection for runtime data.** Requirements that need runtime counters or floats read them from `FRequirementContext::PersistedData`, injected by the owning system before `Evaluate` is called. Never via a component pointer in the context.

---

# How the Pieces Connect

**Authoring.** A designer creates a `URequirementList` Data Asset, sets the `Operator`, adds `URequirement` instances via the Details panel class picker, and configures their properties. Consuming assets hold `TObjectPtr<URequirementList>`. Complex expressions use nested `URequirement_Composite` elements.

**Evaluation (on-demand).** Consuming system constructs `FRequirementContext` and calls `List->Evaluate(Context)` or `List->EvaluateAsync(Context, OnComplete)`. `URequirementLibrary` is an internal helper of `URequirementList` — never called directly.

**Evaluation with payload.** Owning system builds `FRequirementPayload`, inserts it into `FRequirementContext::PersistedData` under a domain tag, then calls `Evaluate`. `URequirement_Persisted` subclasses look up their domain tag and read counters/floats from the payload.

**Evaluation (watched).** Owning system registers a `URequirementList` handle with `URequirementWatcherComponent`. Requirements declare invalidation tags via `GetWatchedEvents`. When a `RequirementEvent.*` tag fires, only watching sets are dirtied. A coalescing timer flushes dirty sets in batches. Owning system receives `FOnRequirementSetDirty`.

**Network.** Each `URequirementList` asset carries `ERequirementEvalAuthority` (`ServerOnly` / `ClientOnly` / `ClientValidated`). `ClientValidated` sets are evaluated on the client for responsiveness; the server always re-evaluates fully from its own context before acting.

---

# File and Folder Structure

```
GameCore/
└── Source/GameCore/
    ├── Requirements/                               ← Core. Zero outgoing module dependencies.
    │   ├── Requirement.h / .cpp                    ← URequirement, URequirement_Persisted
    │   ├── RequirementContext.h                    ← FRequirementContext, FRequirementResult
    │   ├── RequirementPayload.h                    ← FRequirementPayload
    │   ├── RequirementComposite.h / .cpp           ← URequirement_Composite
    │   ├── RequirementLibrary.h / .cpp             ← URequirementLibrary
    │   ├── RequirementSet.h / .cpp                 ← URequirementList, enums, FRequirementSetRuntime
    │   └── RequirementWatcher.h / .cpp             ← URequirementWatcherComponent, URequirementWatcherManager
    │
    ├── Quest/Requirements/
    │   ├── Requirement_QuestCompleted.h / .cpp
    │   ├── Requirement_QuestCooldown.h / .cpp
    │   └── Requirement_ActiveQuestCount.h
    ├── Tags/Requirements/
    │   └── RequirementHasTag.h / .cpp
    └── Leveling/Requirements/
        └── RequirementMinLevel.h / .cpp
```

---

# Network Considerations

| Concern | Approach |
|---|---|
| Authority | Server evaluates before any gated action. |
| Client display | Client may call `Evaluate` locally for UI using replicated data. Non-authoritative. |
| `ClientValidated` sets | Client evaluates for responsiveness; on all-pass, fires Server RPC. Server re-evaluates fully. |
| `ClientOnly` sets | Server never evaluates. Pure UI/cosmetic gating. |
| Context construction | Server derives `FRequirementContext` from RPC connection. Never trusts client-provided subject references. |
| Payload authority | `PersistedData` is built from replicated runtime data — available on both sides. Requirements reading it declare `GetDataAuthority() == Both`. |
| Failure feedback | Consuming system sends a targeted ClientRPC with the `FText` failure reason. |

---

# Known Limitations

- **`EvaluateSetInternal` bypasses `URequirementList` operator.** The watcher flush iterates the flat requirement array directly, ignoring the list's AND/OR operator. Must call `List->Evaluate(Context)` instead. See Design Decisions for full context.
- **Async requirements silently fail in watcher flush.** Flush calls `Evaluate()` (sync). Async requirements return `Fail` by default. `RegisterSet` should reject sets containing async requirements, or the flush must route them through `EvaluateAsync`.
- **Async timeout is implementor's responsibility.** No global timeout. Each async requirement guards via `MakeGuardedCallback`.
- **`EvaluateAllAsync` has no cancellation token.** Guard via `TWeakObjectPtr` capture in lambdas.
- **Blueprint subclassing unvalidated at edit time.** Move Blueprint requirements to C++ before shipping.
- **Watcher flush delay adds latency.** Tunable per system via `FlushDelaySeconds`. Intentional.

---

# Sub-Pages

| Sub-Page | Covers |
|---|---|
| [Supporting Types](Requirement%20System/Supporting%20Types.md) | `FRequirementContext`, `FRequirementResult`, `FRequirementPayload`, `FRequirementSetHandle`, `FRequirementSetRuntime`, all enums. Includes deep explanation of `PersistedData` map design. |
| [Design Decisions](Requirement%20System/Design%20Decisions.md) | System requirements, evolution history (gate check → sets → payload), key architectural decisions with rationale, open issues. |
| [Usage Guide](Requirement%20System/Usage%20Guide.md) | Three usage patterns: one-shot imperative, reactive watched, payload-injected. Full code examples. Common mistakes. Subclass checklist. |
| [`URequirement` — Base Class](Requirement%20System/URequirement%20%E2%80%94%20Base%20Class%20319d261a36cf815f988bc5cacacd5ad0.md) | Full class definition, `URequirement_Persisted`, sync/async implementation examples, async flow diagram, statelessness contract. |
| [`URequirement_Composite`](Requirement%20System/URequirement_Composite%20319d261a36cf81bf84aadce23da6e5a0.md) | `ERequirementOperator`, AND/OR/NOT evaluation logic, async propagation, authoring patterns. |
| [`URequirementLibrary`](Requirement%20System/URequirementLibrary%20319d261a36cf811cab04cd92452e80a3.md) | `EvaluateAll`, `MeetsAll`, `EvaluateAllAsync`, `ValidateRequirements`, `EEvaluateAsyncMode`. |
| [Requirement Sets](Requirement%20System/Requirement%20Sets%2031dd261a36cf8167b97dc63857db467d.md) | `URequirementList`, `ERequirementEvalAuthority`, `ERequirementListOperator`, consuming system integration pattern. |
| [Watcher System](Requirement%20System/Watcher%20System%2031dd261a36cf81dab4b9e7ce3e690bde.md) | `URequirementWatcherComponent`, `URequirementWatcherManager`, event tags, dirty coalescing, `ContextBuilder`, authority and network behaviour. |
