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
| **Supporting Types** | `FRequirementContext`, `FRequirementPayload`, `FRequirementResult`, `FRequirementSetRuntime` | Evaluation inputs, outputs, payload injection, and per-player runtime cache |

---

# Core Design Principles

- **Requirements are definitions, not instances.** `URequirement` objects are authored inside Data Assets and loaded once. They carry no per-player state. The same object is evaluated against many players using only data passed in via `FRequirementContext`.
- **Synchronous by default, async by exception.** `Evaluate` must return immediately for the vast majority of requirements. `EvaluateAsync` exists only for conditions whose data is genuinely not resident in memory at evaluation time.
- **Authority lives on the server.** Systems that gate gameplay actions must evaluate requirements server-side. Client-side evaluation is permitted only for display and UI gating — it is never the authoritative decision.
- **No cross-system imports at the base layer.** `URequirement` and `URequirementLibrary` in `Requirements/` have zero dependencies on other GameCore modules. `FRequirementContext` must never include a typed pointer to any class outside `Requirements/`. Each module that provides requirement types owns those types inside its own folder.
- **Events are GameplayTags, not enums.** Each module registers its own invalidation event tags under `RequirementEvent.*`. The watcher system uses these tags as keys — zero coupling between modules.
- **Composites replace hardcoded logic.** Complex AND/OR/NOT conditions are expressed as `URequirement_Composite` trees — not as custom C++ evaluators with baked-in boolean logic.
- **Watcher evaluation is push-invalidated, pull-evaluated.** Requirements never poll. When a relevant event fires, the affected requirement set is marked dirty. Evaluation runs on the next throttled flush, not on every event.
- **Context payload injection for persisted data.** When a requirement needs to read runtime counter or float data not derivable from world state alone (e.g. quest kill counts), the owning system injects it into `FRequirementContext::PersistedData` before calling `Evaluate`. Requirements that consume this data inherit from `URequirement_Persisted`.

---

# How the Pieces Connect

**Authoring.** A designer creates a `URequirementList` Data Asset, sets the `Operator` (AND or OR), adds `URequirement` instances via the Details panel class picker, and sets their properties. Other assets (ore definitions, quest definitions, crafting recipes) hold a `TObjectPtr<URequirementList>` pointing to that asset. Complex expressions use `URequirement_Composite` children inline within the array.

**Evaluation (on-demand).** The consuming system constructs an `FRequirementContext` and calls `Set->Evaluate(Context)` or `Set->EvaluateAsync(Context, OnComplete)`. `URequirementLibrary` is an internal helper used by `URequirementSet` subclasses — consuming systems never call it directly.

**Evaluation with payload.** When evaluation needs runtime counter data (e.g. quest tracker progress), the owning system builds an `FRequirementPayload` and inserts it into `FRequirementContext::PersistedData` keyed by a domain tag before calling `Evaluate`. `URequirement_Persisted` subclasses read from this map rather than from world state.

**Evaluation (watched).** For systems that need to reactively track availability (quest unlock, ability visibility), the owning system registers a `FRequirementSetRuntime` handle with `URequirementWatcherComponent`. Each `URequirement` in the set declares which `FGameplayTag` events invalidate it via `GetWatchedEvents`. When a `RequirementEvent.*` tag fires through `URequirementWatcherManager`, only sets watching that tag are dirtied. A coalescing timer flushes dirty sets in batches. The owning system receives `OnSetDirty` and re-evaluates.

**Network.** Server is authoritative. Each `URequirementSet` asset carries an `Authority` property (`ERequirementEvalAuthority`, defined in `RequirementSet.h`) set by the designer. `ClientOnly` sets are evaluated on the owning client only (UI, cosmetics). `ClientValidated` sets are evaluated on the client for responsiveness and re-evaluated authoritatively on the server via RPC. `ServerOnly` sets never leave the server. Call sites do not pass or override authority — if two systems need different authority for the same conditions, they reference two separate assets.

---

# `FRequirementPayload`

**File:** `Requirements/RequirementPayload.h`

A keyed data bag injected into `FRequirementContext::PersistedData` at evaluation time. Carries runtime state (counters, floats) that stateless `URequirement` subclasses need to read without coupling to any storage or game system.

The payload is constructed by the owning system and placed into the context before calling `Evaluate`. Requirements never write to this struct — it is read-only at evaluation time.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementPayload
{
    GENERATED_BODY()

    // Integer counters: kill counts, collection counts, interaction counts, etc.
    UPROPERTY(BlueprintReadOnly)
    TMap<FGameplayTag, int32> Counters;

    // Float values: time elapsed, distance travelled, etc.
    UPROPERTY(BlueprintReadOnly)
    TMap<FGameplayTag, float> Floats;

    bool GetCounter(const FGameplayTag& Key, int32& OutValue) const
    {
        if (const int32* Found = Counters.Find(Key))
            { OutValue = *Found; return true; }
        return false;
    }

    bool GetFloat(const FGameplayTag& Key, float& OutValue) const
    {
        if (const float* Found = Floats.Find(Key))
            { OutValue = *Found; return true; }
        return false;
    }
};
```

**Design rules:**
- The key is a **domain tag** (e.g. a `QuestId`), not an individual counter tag. One payload entry per logical domain. Individual counters/floats live inside the payload. This keeps the top-level map small and lookup O(1) at both levels.
- Payload data is available on both server and owning client when built from replicated runtime data (e.g. replicated `FQuestRuntime`). Requirements that read payload must declare `GetDataAuthority() == Both`.
- Payloads are injected via the watcher `ContextBuilder` delegate for reactive evaluation, and via `BuildRequirementContext` for imperative evaluation.

---

# `FRequirementContext`

**File:** `Requirements/RequirementContext.h`

A plain USTRUCT passed by const reference to every `Evaluate` call. This struct lives in `Requirements/` and must have **zero typed dependencies on other GameCore modules or game modules**. Never add a field whose type is defined outside `Requirements/`, `Engine`, or `GameplayTags` — use `PlayerState->FindComponentByClass<T>()` inside the requirement subclass instead.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementContext
{
    GENERATED_BODY()

    // The PlayerState being evaluated against. Always valid on server-side evaluation.
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;

    // World reference for subsystem access, time-of-day, global state.
    UPROPERTY() TObjectPtr<UWorld> World = nullptr;

    // Optional: instigating actor (pawn, NPC, trigger volume, etc.).
    UPROPERTY() TObjectPtr<AActor> Instigator = nullptr;

    // Injected persisted data keyed by payload domain tag (e.g. QuestId).
    // Populated by the owning system's ContextBuilder or BuildRequirementContext
    // before evaluation. Requirements reading this map must declare
    // GetDataAuthority() == Both.
    // Empty for contexts not built by a system that uses payload injection.
    UPROPERTY()
    TMap<FGameplayTag, FRequirementPayload> PersistedData;
};
```

**Rules for adding fields:**
- Only add a field when at least two shipped requirement types require it AND the type is defined in `Engine`, `GameplayTags`, or `Requirements/` itself.
- A requirement that needs a specific component (e.g. `UQuestComponent`, `UInventoryComponent`) must retrieve it via `Context.PlayerState->FindComponentByClass<T>()` inside its own `Evaluate` override. That lookup cost is acceptable — evaluations are not per-frame.
- Never add a typed pointer to any module-specific class here. `FRequirementContext` must compile with no includes beyond Engine and GameplayTags.

---

# `FRequirementResult`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementResult
{
    GENERATED_BODY()

    UPROPERTY() bool bPassed = false;
    UPROPERTY() FText FailureReason;

    static FRequirementResult Pass()                  { return { true,  FText::GetEmpty() }; }
    static FRequirementResult Fail(FText Reason = {}) { return { false, Reason }; }
};
```

---

# `URequirement` — Base Class (Summary)

Abstract base for all evaluatable conditions.

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API URequirement : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirement")
    bool bIsMonotonic = false;

    UFUNCTION(BlueprintNativeEvent, Category = "Requirement")
    void GetWatchedEvents(FGameplayTagContainer& OutEvents) const;

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

# `URequirement_Persisted` — Payload-Reading Base Class

**File:** `Requirements/RequirementPersisted.h / .cpp`

> **Status: Specified in code within the Quest System implementation but not yet promoted to a standalone sub-page in this specification.** The class definition is available in `GameCore Specifications/Quest System/GameCore Changes.md` as an interim reference. A dedicated sub-page should be created here when the Requirement System sub-pages are next updated.

An abstract intermediate class between `URequirement` and any requirement that reads from `FRequirementContext::PersistedData`. Seals `Evaluate()` so subclasses cannot accidentally bypass the payload lookup. Subclasses implement `EvaluateWithPayload(Context, Payload)` instead.

**Key properties:**
- `PayloadKey` (`FGameplayTag`): domain key used to look up `FRequirementPayload` in `FRequirementContext::PersistedData`. Must match the key the owning system injects.
- `GetDataAuthority()` returns `Both`: payload is built from replicated data, available on both server and owning client.
- `Evaluate()` is `final`: subclasses must not override it. Sealed to ensure payload lookup always runs.

See `GameCore Specifications/Quest System/GameCore Changes.md` for the current class definition.

---

# Requirement Sets (Summary)

`URequirementList` is a `UPrimaryDataAsset` with a configurable `Operator` (AND or OR) and an array of `URequirement` children. Any boolean expression is achievable by nesting `URequirement_Composite` elements in the array.

Full specification: see sub-page **Requirement List**.

---

# Watcher System (Summary)

Event-driven reactive evaluation for watched requirement sets. Eliminates polling across all systems.

| Class | Type | Lives On | Role |
| --- | --- | --- | --- |
| `URequirementWatcherManager` | WorldSubsystem | Server + owning Client | Routes `RequirementEvent.*` tag events to the correct player's component |
| `URequirementWatcherComponent` | ActorComponent on `APlayerState` | Server + owning Client | Owns registered sets, dirty flags, coalescing timer, cache |

`RegisterSet` accepts an optional `TFunction<void(FRequirementContext&)> ContextBuilder` delegate. The builder is called immediately before every evaluation flush to inject payload data or other per-evaluation state into the context. This is how `UQuestComponent` injects tracker counters into `FRequirementContext::PersistedData` for completion requirement evaluation.

Full specification: see sub-page **Watcher System**.

---

# Provided Requirement Types

`URequirement`, `URequirement_Composite`, and `URequirement_Persisted` are the only types in `Requirements/`. All other types live inside the module they query.

| Class | Display Name | Owned By | Notes |
| --- | --- | --- | --- |
| `URequirement_Composite` | Composite (AND / OR / NOT) | `Requirements/` | Boolean logic tree |
| `URequirement_Persisted` | *(abstract)* | `Requirements/` | Base for payload-reading requirements |
| `URequirement_HasTag` | Has Gameplay Tag | `Tags/` | Reads replicated tag state |
| `URequirement_QuestCompleted` | Quest Completed | `Quest/Requirements/` | Reads `CompletedQuestTags` from `UQuestComponent` via `FindComponentByClass` |
| `URequirement_QuestCooldown` | Quest Cooldown | `Quest/Requirements/` | Reads `LastCompletedTimestamp` from `UQuestComponent` via `FindComponentByClass` |
| `URequirement_ActiveQuestCount` | Active Quest Capacity | `Quest/Requirements/` | Reads `ActiveQuests` count from `UQuestComponent` via `FindComponentByClass` |

---

# Adding a New Requirement Type

1. Subclass `URequirement` (or `URequirement_Persisted` if reading payload data) inside the module that owns the data being queried.
2. Add `DisplayName = "..."` and `Blueprintable` to the `UCLASS` macro.
3. Add `"Requirements"` to `PublicDependencyModuleNames` in `Build.cs`.
4. Declare config properties as `EditDefaultsOnly BlueprintReadOnly UPROPERTY`.
5. Override `Evaluate` (sync) or `IsAsync` + `EvaluateAsync` (async). For `URequirement_Persisted` subclasses: override `EvaluateWithPayload` instead.
6. Override `GetWatchedEvents` to declare which `RequirementEvent.*` tags invalidate this requirement.
7. Set `bIsMonotonic = true` in the CDO or Details panel if the condition can never revert once passed.
8. Override `GetDescription()` inside `#if WITH_EDITOR`.

**No central registry. No factory. No modification to `URequirement` itself.**

---

# Network Considerations

| Concern | Approach |
| --- | --- |
| Authority | Server evaluates before any gated action. |
| Client display | Client may call `Evaluate` locally for UI using replicated `PlayerState` data and replicated context payload. Non-authoritative. |
| `ClientValidated` sets | Client evaluates for responsiveness; on all-pass, fires Server RPC. Server re-evaluates fully — never trusts client result. |
| `ClientOnly` sets | Server never evaluates. Pure UI/cosmetic gating. |
| Context construction | Server derives `FRequirementContext` from RPC connection — never trusts client-provided subject references. |
| Payload authority | `FRequirementContext::PersistedData` is built client-side from replicated runtime data. Requirements reading it must declare `GetDataAuthority() == Both`. |
| Failure feedback | Consuming system sends a targeted ClientRPC with the `FText` failure reason. |

---

# File and Folder Structure

```
GameCore/
└── Source/GameCore/
    ├── Requirements/                               ← Core. Zero outgoing dependencies.
    │   ├── Requirement.h / .cpp                    ← URequirement base
    │   ├── RequirementContext.h                    ← FRequirementContext, FRequirementResult
    │   ├── RequirementPayload.h                    ← FRequirementPayload
    │   ├── RequirementPersisted.h / .cpp           ← URequirement_Persisted
    │   ├── RequirementComposite.h / .cpp           ← URequirement_Composite
    │   ├── RequirementLibrary.h / .cpp             ← URequirementLibrary
    │   ├── RequirementSet.h / .cpp                 ← URequirementSet, URequirementList
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

# Known Limitations

- **`FRequirementContext` field growth.** Resist adding fields. If a requirement needs a reference not in the context, retrieve it via `PlayerState->FindComponentByClass<T>()` inside the requirement subclass. That is the correct and only acceptable pattern.
- **Async timeout is implementor's responsibility.** The library has no global async timeout. Each async requirement must guard against backend non-response.
- **`EvaluateAllAsync` has no cancellation token.** Guard with `TWeakObjectPtr` capture in the lambda.
- **Blueprint subclassing is unvalidated at edit time.** Blueprint requirements on hot evaluation paths should be moved to C++ before shipping.
- **Watcher flush delay adds latency.** Quest unlock or ability availability may lag up to the flush delay after the triggering event. This is intentional — tune delay per system.
- **`URequirement_Persisted` has no standalone sub-page yet.** See the `GameCore Changes.md` cross-reference in the Quest System spec for the interim class definition.

---

# Sub-Pages

| Sub-Page | Covers |
| --- | --- |
| `URequirement` — Base Class | Abstract base class, sync/async contracts, `bIsMonotonic`, `GetWatchedEvents`, statelessness rules, subclassing checklist |
| `URequirement_Composite` | `ERequirementOperator`, AND/OR/NOT evaluation logic, async propagation, authoring patterns |
| `URequirementLibrary` | `EvaluateAll`, `MeetsAll`, `EvaluateAllAsync`, `ValidateRequirements`, `EEvaluateAsyncMode` |
| Requirement Sets | `URequirementSet`, `URequirementList`, `FRequirementSetRuntime`, authority flags |
| Watcher System | `URequirementWatcherComponent`, `URequirementWatcherManager`, event tags, dirty coalescing, cache, `ContextBuilder` |

[`URequirement_Composite`](Requirement%20System/URequirement_Composite%20319d261a36cf81bf84aadce23da6e5a0.md)

[`URequirement` — Base Class](Requirement%20System/URequirement%20%E2%80%94%20Base%20Class%20319d261a36cf815f988bc5cacacd5ad0.md)

[`URequirementLibrary`](Requirement%20System/URequirementLibrary%20319d261a36cf811cab04cd92452e80a3.md)

[Requirement Sets](Requirement%20System/Requirement%20Sets%2031dd261a36cf8167b97dc63857db467d.md)

[Watcher System](Requirement%20System/Watcher%20System%2031dd261a36cf81dab4b9e7ce3e690bde.md)
