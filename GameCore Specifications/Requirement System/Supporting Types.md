# Supporting Types

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

All plain structs, enums, and handles used across the Requirement System. None of these types have outgoing dependencies on other GameCore modules — they are defined entirely within `Requirements/`.

**Files:**
- `Requirements/RequirementContext.h` — `FRequirementContext`, `FRequirementResult`
- `Requirements/RequirementPayload.h` — `FRequirementPayload`
- `Requirements/RequirementSet.h` — `FRequirementSetHandle`, `FRequirementSetRuntime`, enums

---

# `FRequirementResult`

The return type of every `Evaluate` and `EvaluateAsync` call. Carries a pass/fail flag and an optional player-facing failure reason.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementResult
{
    GENERATED_BODY()

    UPROPERTY() bool bPassed = false;

    // Optional player-facing explanation of why the check failed.
    // Used by consuming systems to surface feedback in UI.
    // Empty on Pass results.
    UPROPERTY() FText FailureReason;

    static FRequirementResult Pass()
    {
        return { true, FText::GetEmpty() };
    }

    static FRequirementResult Fail(FText Reason = {})
    {
        return { false, Reason };
    }
};
```

**Notes:**
- Always construct via `Pass()` / `Fail()` — never set `bPassed` directly.
- `FailureReason` is localisation-ready (`FText`). Do not use `FString`.
- Consuming systems forward `FailureReason` to the client via a targeted RPC for display. The server never shows UI directly.

---

# `FRequirementPayload`

A flat data bag carrying integer counters and float values. One payload represents the runtime state of a single logical domain (e.g. all tracker data for one quest). It is built by the owning system and injected into `FRequirementContext::PersistedData` before `Evaluate` is called. Requirements never write to this struct.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementPayload
{
    GENERATED_BODY()

    // Integer counters: kill counts, collection counts, interaction counts, etc.
    // Key is a GameplayTag identifying the specific counter within this domain.
    UPROPERTY(BlueprintReadOnly)
    TMap<FGameplayTag, int32> Counters;

    // Float values: time elapsed, distance travelled, percentages, etc.
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

---

# `FRequirementContext`

The evaluation input. Passed by `const&` to every `Evaluate` call. Must never include a typed pointer to any class defined outside `Requirements/`, `Engine`, or `GameplayTags`. Requirements that need a specific component (e.g. `UQuestComponent`, `UInventoryComponent`) retrieve it via `Context.PlayerState->FindComponentByClass<T>()` inside their own `Evaluate` override.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementContext
{
    GENERATED_BODY()

    // The PlayerState being evaluated against.
    // Always valid during server-side evaluation.
    // Used by requirements to reach replicated components via FindComponentByClass.
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;

    // World reference for subsystem access, time-of-day queries, global state.
    UPROPERTY() TObjectPtr<UWorld> World = nullptr;

    // Optional instigating actor: pawn, NPC, trigger volume, interactable, etc.
    // Null for evaluations not triggered by a specific actor.
    UPROPERTY() TObjectPtr<AActor> Instigator = nullptr;

    // Injected runtime payload data.
    // Key:   domain tag identifying the owning system (e.g. QuestId, FactionId).
    // Value: FRequirementPayload holding the counters/floats for that domain.
    //
    // Populated by the owning system's ContextBuilder (watcher path) or
    // BuildRequirementContext helper (imperative path) before Evaluate is called.
    //
    // Requirements reading this map must declare GetDataAuthority() == Both,
    // since payload is built from replicated data available on both sides.
    // Empty for contexts not produced by a system that uses payload injection.
    UPROPERTY()
    TMap<FGameplayTag, FRequirementPayload> PersistedData;
};
```

**Rules for adding fields:**
- Only add a field when at least two shipped requirement types require it AND the type is defined in `Engine`, `GameplayTags`, or `Requirements/` itself.
- Never add a typed pointer to a module-specific class. Use `PlayerState->FindComponentByClass<T>()` inside the requirement subclass instead.
- `PersistedData` covers all runtime counter/float needs. Adding parallel data bags is a design error — extend `FRequirementPayload` if a new value type is needed.

---

## Why `PersistedData` is `TMap<FGameplayTag, FRequirementPayload>` and not just `FRequirementPayload`

This is the most important design decision in the context type and warrants a full explanation.

### The problem with a flat payload

Imagine `PersistedData` were a single `FRequirementPayload` — one flat map of `FGameplayTag → int32` counters. A `URequirementList` on a quest stage might contain:

- `URequirement_TrackerCount` checking `Quest.Counter.KillCount >= 10` (injected by `UQuestComponent`)
- `URequirement_ReputationThreshold` checking `Reputation.Counter.Merchant >= 500` (injected by `UReputationComponent`)
- `URequirement_TrackerCount` for a second quest tracker checking `Quest.Counter.ItemsDelivered >= 3`

With a flat payload, all three counters share the same map. The keys are `Quest.Counter.KillCount`, `Reputation.Counter.Merchant`, and `Quest.Counter.ItemsDelivered`. This works until:

1. **Two quests are active simultaneously.** Quest A tracks `Quest.Counter.KillCount` at 7. Quest B also tracks `Quest.Counter.KillCount` at 2. Which value goes into the flat map? There is no answer — there is a collision. The second quest's `ContextBuilder` would overwrite the first's counter.

2. **Two independent systems inject counters into the same flush.** `UQuestComponent` writes its counters. `UReputationComponent` writes its counters. With a flat map, each system must defensively avoid touching the other's keys — there is no enforced namespace separation. Any key collision silently produces a wrong evaluation result.

### The two-level map solution

`TMap<FGameplayTag, FRequirementPayload>` adds one level of indirection: the **domain tag**.

```
PersistedData
  ├── Quest.Id.TreasureHunt   →  FRequirementPayload { Counters: { Quest.Counter.KillCount: 7,
  │                                                                   Quest.Counter.ItemsDelivered: 0 } }
  ├── Quest.Id.EscortMission  →  FRequirementPayload { Counters: { Quest.Counter.KillCount: 2 } }
  └── Faction.Id.Merchant     →  FRequirementPayload { Counters: { Reputation.Counter.Standing: 500 } }
```

Each system owns exactly one key in the outer map — its domain tag. `UQuestComponent` writes under `Quest.Id.TreasureHunt` and `Quest.Id.EscortMission`. `UReputationComponent` writes under `Faction.Id.Merchant`. They cannot collide. The inner `FRequirementPayload` uses counter tags that are local to that domain — `Quest.Counter.KillCount` under the TreasureHunt domain and the EscortMission domain are entirely separate entries.

`URequirement_Persisted` subclasses store a `PayloadKey` property (the domain tag, e.g. the QuestId the requirement belongs to). At evaluation time:

```cpp
// Inside URequirement_Persisted::Evaluate (sealed final):
const FRequirementPayload* Payload = Context.PersistedData.Find(PayloadKey);
if (!Payload) return FRequirementResult::Fail(/* payload not injected */);
return EvaluateWithPayload(Context, *Payload); // subclass reads from Payload
```

The requirement is self-contained — it knows its domain tag, looks up its payload, and never touches other domains.

### Summary

| Design | Key collision risk | Multi-domain support | Clear ownership |
|---|---|---|---|
| Flat `FRequirementPayload` | Yes — same key space | No | No |
| `TMap<FGameplayTag, FRequirementPayload>` | None — namespaced | Yes | Yes — one entry per system |

The outer map stays small (one entry per active system injecting data — typically 1–4 in practice). Both levels are O(1) lookup via `TMap`. The total memory overhead is negligible.

---

# `FRequirementSetHandle`

An opaque identifier issued by `URequirementWatcherComponent::RegisterSet`. The owning system stores this and passes it to `UnregisterSet` when it no longer needs reactive tracking.

```cpp
struct FRequirementSetHandle
{
    uint32 Id = 0;
    bool IsValid() const { return Id != 0; }
    bool operator==(const FRequirementSetHandle& Other) const { return Id == Other.Id; }
};

USTRUCT()
struct FRequirementSetHandleKeyFuncs : public DefaultKeyFuncs<FRequirementSetHandle>
{
    static uint32 GetKeyHash(const FRequirementSetHandle& H) { return H.Id; }
};
```

Handles are monotonically increasing per-component instance. Handle `0` is always invalid. There is no global handle registry.

---

# `FRequirementSetRuntime`

Per-player cache for one registered `URequirementList`. Lives entirely inside `URequirementWatcherComponent`. Never replicated. Never part of the shared asset.

```cpp
USTRUCT()
struct FRequirementSetRuntime
{
    GENERATED_BODY()

    // Shared list asset. Not per-player — many players reference the same asset.
    UPROPERTY()
    TObjectPtr<URequirementList> Asset;

    // Parallel to Asset->GetAllRequirements().
    // Sized at RegisterSet time. Never resized after.
    // CachedTrue entries for monotonic requirements are never re-evaluated.
    TArray<ERequirementCacheState> CachedResults;

    // Read from Asset->Authority at registration time.
    ERequirementEvalAuthority Authority = ERequirementEvalAuthority::ServerOnly;

    // Unique handle for this registration.
    FRequirementSetHandle Handle;

    // Delegate fired on the owning system after every re-evaluation.
    FOnRequirementSetDirty OnDirty;

    // Optional. Called immediately before evaluation to inject payload data.
    // Captured lambda — owning system injects whatever context it needs.
    // Must use TWeakObjectPtr for any UObject captured in the lambda.
    // Must not call RegisterSet/UnregisterSet (re-entrant mutation not supported).
    TFunction<void(FRequirementContext&)> ContextBuilder;
};
```

---

# Enums

## `ERequirementDataAuthority`

Declared on each `URequirement` subclass via `GetDataAuthority()`. Describes where the data this requirement reads is available at runtime. Used by `ValidateRequirements` at `BeginPlay` to catch mismatches between requirement data locality and list evaluation authority.

```cpp
UENUM()
enum class ERequirementDataAuthority : uint8
{
    // Reads only locally-available data: replicated PlayerState fields,
    // replicated components, local UI state.
    // Safe in ServerOnly, ClientOnly, and ClientValidated lists.
    ClientOnly,

    // Reads data that only exists on the server: non-replicated subsystems,
    // authoritative DB state, server-only component fields.
    // Must only appear in ServerOnly lists.
    // In ClientValidated or ClientOnly lists this requirement cannot be evaluated
    // on the client — ValidateRequirements treats this as a design error.
    ServerOnly,

    // Has a correct, meaningful evaluation on both server and client.
    // Implementor handles any data availability difference inside Evaluate().
    // Safe in any list authority.
    Both,
};
```

## `ERequirementEvalAuthority`

Declared once on the `URequirementList` asset by the designer. Controls which side evaluates the list. Consuming systems read `List->Authority` — they never pass or override it.

```cpp
UENUM(BlueprintType)
enum class ERequirementEvalAuthority : uint8
{
    // Server evaluates only. Client never receives a result until the server decides.
    // Use for all gameplay-gating checks: loot, quest progression, ability use.
    ServerOnly UMETA(DisplayName = "Server Only"),

    // Client evaluates only. Server never checks.
    // Use for purely cosmetic or UI-gating checks: tooltip visibility, button greying.
    // All requirements in a ClientOnly list must return ClientOnly or Both from
    // GetDataAuthority(). A ServerOnly requirement here is a design error.
    ClientOnly UMETA(DisplayName = "Client Only"),

    // Client evaluates for responsiveness. On all-pass, fires a Server RPC.
    // Server re-evaluates fully from its own context — never trusts the client result.
    // Use for player-facing unlocks where immediate UI feedback matters.
    // All requirements must return ClientOnly or Both from GetDataAuthority().
    ClientValidated UMETA(DisplayName = "Client Validated"),
};
```

> **Security note.** In `ClientValidated` mode the server RPC must trigger a full re-evaluation using a server-constructed `FRequirementContext`. The client result is a hint only.

## `ERequirementListOperator`

```cpp
UENUM(BlueprintType)
enum class ERequirementListOperator : uint8
{
    // All requirements must pass. Short-circuits on first failure.
    AND UMETA(DisplayName = "All Must Pass (AND)"),

    // Any single requirement passing is sufficient. Short-circuits on first pass.
    OR  UMETA(DisplayName = "Any Must Pass (OR)"),
};
```

## `ERequirementCacheState`

```cpp
UENUM()
enum class ERequirementCacheState : uint8
{
    Uncached,    // Not yet evaluated.
    CachedFalse, // Last result was Fail. Re-evaluated on next dirty flush.
    CachedTrue,  // Last result was Pass. Permanent if bIsMonotonic == true.
};
```
