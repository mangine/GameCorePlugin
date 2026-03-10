# Requirement System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Requirement System is a data-driven, polymorphic condition evaluation layer. It provides a single, reusable mechanism for expressing and evaluating prerequisites — usable by any system (quests, interactions, abilities, dialogue, crafting) without coupling those systems together. Requirements are configured directly in Data Assets using instanced UObjects with designer-friendly properties. Evaluation is synchronous by default; an async path exists for conditions that depend on data not resident in memory. Evaluation authority always lives on the server.

---

# System Modules

| Module | Classes | Role |
| --- | --- | --- |
| **Core Evaluation** | `URequirement`, `URequirement_Composite`, `URequirementLibrary` | Abstract base, boolean logic tree, evaluation entry point |
| **Requirement Sets** | `URequirementSet`, `URequirementList` | Grouping layer: list (AND-all). OR/NOT logic expressed inline via `URequirement_Composite` children. |
| **Watcher System** | `URequirementWatcherComponent`, `URequirementWatcherManager` | Event-driven per-player dirty tracking. Eliminates polling. |
| **Supporting Types** | `FRequirementContext`, `FRequirementResult`, `FRequirementSetRuntime` | Evaluation inputs, outputs, and per-player runtime cache |

---

# Core Design Principles

These principles are the constraints everything else is built around. When in doubt about any design decision in this system or in systems consuming it, refer back here.

- **Requirements are definitions, not instances.** `URequirement` objects are authored inside Data Assets and loaded once. They carry no per-player state. The same object is evaluated against many players using only data passed in via `FRequirementContext`.
- **Synchronous by default, async by exception.** `Evaluate` must return immediately for the vast majority of requirements. `EvaluateAsync` exists only for conditions whose data is genuinely not resident in memory at evaluation time.
- **Authority lives on the server.** Systems that gate gameplay actions must evaluate requirements server-side. Client-side evaluation is permitted only for display and UI gating — it is never the authoritative decision.
- **No cross-system imports at the base layer.** `URequirement` and `URequirementLibrary` in `Requirements/` have zero dependencies on other GameCore modules. Each module that provides requirement types owns those types inside its own folder.
- **Events are GameplayTags, not enums.** Each module registers its own invalidation event tags under `RequirementEvent.*`. The watcher system uses these tags as keys — zero coupling between modules.
- **Composites replace hardcoded logic.** Complex AND/OR/NOT conditions are expressed as `URequirement_Composite` trees — not as custom C++ evaluators with baked-in boolean logic.
- **Watcher evaluation is push-invalidated, pull-evaluated.** Requirements never poll. When a relevant event fires, the affected requirement set is marked dirty. Evaluation runs on the next throttled flush, not on every event.

---

# How the Pieces Connect

**Authoring.** A designer creates a `URequirementList` Data Asset, sets the `Operator` (AND or OR), adds `URequirement` instances via the Details panel class picker, and sets their properties. Other assets (ore definitions, quest definitions, crafting recipes) hold a `TObjectPtr<URequirementList>` pointing to that asset. Complex expressions use `URequirement_Composite` children inline within the array.

**Evaluation (on-demand).** The consuming system constructs an `FRequirementContext` and calls `Set->Evaluate(Context)` or `Set->EvaluateAsync(Context, OnComplete)`. `URequirementLibrary` is an internal helper used by `URequirementSet` subclasses — consuming systems never call it directly.

**Evaluation (watched).** For systems that need to reactively track availability (quest unlock, ability visibility), the owning system registers a `FRequirementSetRuntime` handle with `URequirementWatcherComponent`. Each `URequirement` in the set declares which `FGameplayTag` events invalidate it via `GetWatchedEvents`. When a `RequirementEvent.*` tag fires through `URequirementWatcherManager`, only sets watching that tag are dirtied. A coalescing timer flushes dirty sets in batches. The owning system receives `OnSetDirty` and re-evaluates.

**Network.** Server is authoritative. Each `URequirementSet` asset carries an `Authority` property (`ERequirementEvalAuthority`, defined in `RequirementSet.h`) set by the designer. `ClientOnly` sets are evaluated on the owning client only (UI, cosmetics). `ClientValidated` sets are evaluated on the client for responsiveness and re-evaluated authoritatively on the server via RPC. `ServerOnly` sets never leave the server. Call sites do not pass or override authority — if two systems need different authority for the same conditions, they reference two separate assets.

---

# `FRequirementContext`

A plain USTRUCT passed by const reference to every `Evaluate` call. Kept deliberately narrow.

```cpp
USTRUCT(BlueprintType)
struct FRequirementContext
{
    GENERATED_BODY()

    // The PlayerState being evaluated against. Always valid on server-side evaluation.
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;

    // World reference for subsystem access, time-of-day, global state.
    UPROPERTY() TObjectPtr<UWorld> World = nullptr;

    // Optional: instigating actor (pawn, NPC, trigger volume, etc.).
    UPROPERTY() TObjectPtr<AActor> Instigator = nullptr;
};
```

**Rules for adding fields:** Only add a field when at least two shipped requirement types require it. A field needed by only one subclass belongs in that subclass via subsystem or component lookup. Never add game-specific types here.

---

# `FRequirementResult`

Returned by every synchronous `Evaluate` call and by `EvaluateAll`.

```cpp
USTRUCT(BlueprintType)
struct FRequirementResult
{
    GENERATED_BODY()

    UPROPERTY() bool bPassed = false;

    // Optional. First failure reason in the evaluated set.
    UPROPERTY() FText FailureReason;

    static FRequirementResult Pass()                        { return { true,  FText::GetEmpty() }; }
    static FRequirementResult Fail(FText Reason = {})       { return { false, Reason }; }
};
```

---

# `URequirement` — Base Class (Summary)

Abstract base for all evaluatable conditions. Carries two new fields introduced by the Watcher System; the evaluation contract is unchanged.

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API URequirement : public UObject
{
    GENERATED_BODY()
public:
    // ── Watcher System ────────────────────────────────────────────────────────

    // If true, once this requirement passes it can never revert.
    // (e.g. level reached, quest completed, achievement unlocked.)
    // The watcher caches a permanent Pass and skips re-evaluation after that point.
    // Set this in the Details panel for Blueprint subclasses, or override in C++.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirement")
    bool bIsMonotonic = false;

    // Populates OutEvents with the GameplayTags that, when fired through
    // URequirementWatcherManager, should dirty any set containing this requirement.
    // Default: empty (requirement is never automatically dirtied — manual re-evaluation only).
    // Override in every requirement that participates in watched evaluation.
    UFUNCTION(BlueprintNativeEvent, Category = "Requirement")
    void GetWatchedEvents(FGameplayTagContainer& OutEvents) const;

    // ── Evaluation (unchanged) ─────────────────────────────────────────────────
    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const;
    virtual bool IsAsync() const { return false; }
    virtual void EvaluateAsync(const FRequirementContext& Context,
                               TFunction<void(FRequirementResult)> OnComplete) const;

#if WITH_EDITOR
    virtual FString GetDescription() const;
#endif
};
```

Full class definition, statelessness contract, and subclassing checklist: see sub-page **`URequirement` — Base Class**.

---

# Requirement Sets (Summary)

`URequirementList` is a `UPrimaryDataAsset` with a configurable `Operator` (AND or OR) and an array of `URequirement` children. `URequirementSet` has been removed — `URequirementList` is the sole class. Any boolean expression is achievable by nesting `URequirement_Composite` elements in the array.

Full specification: see sub-page **Requirement List**.

---

# Watcher System (Summary)

Event-driven reactive evaluation for watched requirement sets. Eliminates polling across all systems.

| Class | Type | Lives On | Role |
| --- | --- | --- | --- |
| `URequirementWatcherManager` | WorldSubsystem | Server + owning Client | Routes `RequirementEvent.*` tag events to the correct player's component |
| `URequirementWatcherComponent` | ActorComponent on `APlayerState` | Server + owning Client | Owns registered sets, dirty flags, coalescing timer, cache |

At startup the component does a full evaluation pass. After that, only dirty sets are re-evaluated. Flush delay is configurable per owning system (default 0.5s; UI systems may use 0.1s).

Full specification: see sub-page **Watcher System**.

---

# Provided Requirement Types

`URequirement` and `URequirement_Composite` are the only types in `Requirements/`. All other types live inside the module they query.

| Class | Display Name | Owned By | Configurable Properties |
| --- | --- | --- | --- |
| `URequirement_Composite` | Composite (AND / OR / NOT) | `Requirements/` | `Operator`, `Children[]` |
| `URequirement_HasTag` | Has Gameplay Tag | `Tags/` | `RequiredTag` |
| `URequirement_MinLevel` | Minimum Player Level | `Leveling/` | `MinLevel` (int32) |
| `URequirement_QuestCompleted` | Quest Completed | `Quests/` | `QuestId` (FName) |
| `URequirement_QuestActive` | Quest Active | `Quests/` | `QuestId` (FName) |
| `URequirement_TimeOfDay` | Time Of Day Window | `TimeOfDay/` | `StartHour`, `EndHour` (float) |

---

# Adding a New Requirement Type

1. Subclass `URequirement` inside the module that owns the data being queried.
2. Add `DisplayName = "..."` and `Blueprintable` to the `UCLASS` macro.
3. Add `"Requirements"` to `PublicDependencyModuleNames` in `Build.cs`.
4. Declare config properties as `EditDefaultsOnly BlueprintReadOnly UPROPERTY`.
5. Override `Evaluate` (sync) or `IsAsync` + `EvaluateAsync` (async).
6. Override `GetWatchedEvents` to declare which `RequirementEvent.*` tags invalidate this requirement.
7. Set `bIsMonotonic = true` in the CDO or Details panel if the condition can never revert once passed.
8. Override `GetDescription()` inside `#if WITH_EDITOR`.

**No central registry. No factory. No modification to `URequirement` itself.**

---

# Network Considerations

The core Requirement System has no replication code — it is a pure evaluation layer. The Watcher System adds network-aware authority handling.

| Concern | Approach |
| --- | --- |
| Authority | Server evaluates before any gated action. Both sync and async paths. |
| Client display | Client may call `EvaluateAll` locally for UI using replicated PlayerState data. Non-authoritative. |
| `ClientValidated` sets | Client evaluates for responsiveness; on all-pass, fires Server RPC. Server re-evaluates fully — never trusts client result. |
| `ClientOnly` sets | Server never evaluates. Pure UI/cosmetic gating. |
| Context construction | Server derives `FRequirementContext` from RPC connection — never trusts client-provided subject references. |
| Failure feedback | Consuming system sends a targeted ClientRPC with the `FText` failure reason. |

---

# File and Folder Structure

```jsx
GameCore/
└── Source/GameCore/
    ├── Requirements/                               ← Core. Zero outgoing dependencies.
    │   ├── Requirement.h / .cpp                    ← URequirement base
    │   ├── RequirementComposite.h / .cpp           ← URequirement_Composite
    │   ├── RequirementLibrary.h / .cpp             ← URequirementLibrary
    │   ├── RequirementSet.h / .cpp                 ← URequirementSet, URequirementList
    │   └── RequirementWatcher.h / .cpp             ← URequirementWatcherComponent, URequirementWatcherManager
    │
    ├── Tags/Requirements/
    │   └── RequirementHasTag.h / .cpp
    ├── Leveling/Requirements/
    │   └── RequirementMinLevel.h / .cpp
    ├── Quests/Requirements/
    │   ├── RequirementQuestCompleted.h / .cpp
    │   └── RequirementQuestActive.h / .cpp
    └── TimeOfDay/Requirements/
        └── RequirementTimeOfDay.h / .cpp
```

---

# Known Limitations

- **`FRequirementContext` field growth.** Resist adding fields. If a requirement needs a reference not in the context, prefer subsystem or component lookup.
- **Async timeout is implementor's responsibility.** The library has no global async timeout. Each async requirement must guard against backend non-response.
- **`EvaluateAllAsync` has no cancellation token.** Guard with `TWeakObjectPtr` capture in the lambda.
- **Blueprint subclassing is unvalidated at edit time.** Blueprint requirements on hot evaluation paths should be moved to C++ before shipping.
- **Watcher flush delay adds latency.** Quest unlock or ability availability may lag up to the flush delay after the triggering event. This is intentional — tune delay per system.

---

# Sub-Pages

| Sub-Page | Covers |
| --- | --- |
| `URequirement` — Base Class | Abstract base class, sync/async contracts, `bIsMonotonic`, `GetWatchedEvents`, statelessness rules, subclassing checklist |
| `URequirement_Composite` | `ERequirementOperator`, AND/OR/NOT evaluation logic, async propagation, authoring patterns |
| `URequirementLibrary` | `EvaluateAll`, `MeetsAll`, `EvaluateAllAsync`, `ValidateRequirements`, `EEvaluateAsyncMode` |
| Requirement Sets | `URequirementSet`, `URequirementList`, `FRequirementSetRuntime`, authority flags |
| Watcher System | `URequirementWatcherComponent`, `URequirementWatcherManager`, event tags, dirty coalescing, cache |

[`URequirement_Composite`](Requirement%20System/URequirement_Composite%20319d261a36cf81bf84aadce23da6e5a0.md)

[`URequirement` — Base Class](Requirement%20System/URequirement%20%E2%80%94%20Base%20Class%20319d261a36cf815f988bc5cacacd5ad0.md)

[`URequirementLibrary`](Requirement%20System/URequirementLibrary%20319d261a36cf811cab04cd92452e80a3.md)

[Requirement Sets](Requirement%20System/Requirement%20Sets%2031dd261a36cf8167b97dc63857db467d.md)

[Watcher System](Requirement%20System/Watcher%20System%2031dd261a36cf81dab4b9e7ce3e690bde.md)