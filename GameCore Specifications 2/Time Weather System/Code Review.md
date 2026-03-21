# Time Weather System — Code Review

**Part of:** GameCore Plugin | **Status:** Active Specification | **UE Version:** 5.7

Architectural review of the Time Weather System specification. Covers design flaws, code quality issues, and improvement suggestions.

---

## Summary

The system is well-conceived overall. The deterministic, persistence-free approach is correct for an MMORPG where restarting the server should produce seamless continuity. The `FWeatherBlendState` single-output model is clean, and the sequence abstraction is genuinely extensible. The issues below are real and worth addressing before implementation, but none are fundamental architecture failures.

---

## Issue 1: Plugin Coupling to Game Module AGameState — CRITICAL

**Location:** `UTimeWeatherSubsystem::PushSnapshotToClients`

```cpp
if (AMyGameState* GS = GetWorld()->GetGameState<AMyGameState>())
```

`AMyGameState` is a game-module class. This hard-couples the `GameCore` plugin to the game module, violating the plugin's core decoupling principle. The plugin must not know what `AMyGameState` is.

**Fix:** Expose an interface or delegate pattern:

```cpp
// Option A: Interface on AGameStateBase
UINTERFACE()
class UTimeSnapshotReceiver : public UInterface { GENERATED_BODY() };

class ITimeSnapshotReceiver
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent)
    void SetTimeSnapshot(const FGameTimeSnapshot& Snapshot);
};

// PushSnapshotToClients becomes:
if (AGameStateBase* GS = GetWorld()->GetGameState())
    if (GS->Implements<UTimeSnapshotReceiver>())
        ITimeSnapshotReceiver::Execute_SetTimeSnapshot(GS, TimeSnapshot);
```

Option B: expose a delegate `OnTimeSnapshotReady` that the game module binds to. Either approach removes the `AMyGameState` reference from the plugin.

---

## Issue 2: `ResolveSeasonContext` is Not Const Despite Being a Query — MODERATE

**Location:** `UTimeWeatherSubsystem::GetDaylightIntensity`

```cpp
FSeasonContext SeasonCtx = ResolveSeasonContext(
    const_cast<FContextState&>(*State), ...);
```

`ResolveSeasonContext` takes `FContextState&` (non-const) but performs no mutation. The `const_cast` to call it from a `const` query method is a code smell and unsafe if anyone adds mutation to the method later.

**Fix:** Make `ResolveSeasonContext` take `const FContextState&`. It reads `State.SeasonRanges` and `State.CurrentBlend.BaseWeatherB` — both are read-only in the resolution path. Remove `const_cast`.

---

## Issue 3: `FTimeEvent_DawnDusk.ContextId` is Wrong Type — MODERATE

**Location:** Original spec `GMS Events.md`, `FTimeEvent_DawnDusk`

The original spec declared:
```cpp
FGameplayTag ContextId; // which context
```

All other payloads (`FTimeEvent_SeasonChanged`, `FWeatherEvent_StateChanged`, etc.) use `FGuid ContextId`. This is inconsistent and meaningless — context identity is a GUID, not a gameplay tag.

**Fix (applied in this spec):** Changed `FTimeEvent_DawnDusk::ContextId` to `FGuid ContextId`. `FGuid()` = global context, matching all other payloads.

---

## Issue 4: `RollTimedEvents` Has Orphan Code Reference in Original Spec — MODERATE

**Location:** Original `Data Assets.md`, `RollTimedEvents` pseudocode

The original spec's `RollTimedEvents` pseudocode inside `Data Assets.md` referenced `FContextState::ScheduledEventTriggers` but incorrectly computed `DayStartSeconds` using `State.Context /* resolved DayDurationSeconds */` — this was placeholder pseudocode that would not compile. The canonical implementation lives in `UTimeWeatherSubsystem` and is what matters; the pseudocode in `Data Assets.md` was removed in this migration.

---

## Issue 5: `TriggerOverlayEvent` Priority Logic Treats Equal Priority as "Should Queue" — DESIGN FLAW

**Location:** `UTimeWeatherSubsystem::TriggerOverlayEvent`

```cpp
if (Existing.Priority >= NewPriority)  // >= means equal priority also queues
{ bShouldQueue = true; break; }
```

The requirement says: "Equal priority: first-registered wins." This is correctly implemented via `>=` — an equal-priority new event is queued behind the existing one. However the code comment and the queued-events sort also use `>=`:

```cpp
if (State->QueuedEvents[i].Priority < NewPriority)  // inserts before lower priority only
```

This means equal-priority events are appended after all other equal-priority events in the queue (FIFO within priority band). This is correct per the spec, but it is not obvious from the code. A comment explaining the invariant would prevent future bugs.

**Fix:** Add a comment, not a code change. The logic is correct.

---

## Issue 6: `ActivateQueuedEvent` Does Not Respect Existing Active Events — BUG

**Location:** `UTimeWeatherSubsystem::ActivateQueuedEvent`

After an active event expires and `ActivateQueuedEvent` is called, the method unconditionally promotes the first queued event and inserts it at `ActiveEvents[0]`. But if multiple active events exist (the spec allows multiple in `ActiveEvents`, not just one), the newly promoted event may have lower priority than a surviving active event.

In practice the spec states "Only the first entry drives `OverlayWeather`", and the priority order of `ActiveEvents` is assumed to be maintained. However `ActivateQueuedEvent` inserts with `Insert(NewEvent, 0)` without checking whether the remaining active events have higher priority.

**Fix:** After promoting a queued event, sort `ActiveEvents` by priority descending (or perform an insertion-sort) to guarantee index 0 is always the highest-priority active event.

```cpp
State.ActiveEvents.Insert(NewEvent, 0);
// Re-sort to guarantee priority ordering is maintained.
State.ActiveEvents.Sort([](const FActiveWeatherEvent& A, const FActiveWeatherEvent& B)
    { return A.Priority > B.Priority; });
```

---

## Issue 7: `GetBlendedWeatherState` Tag Flip at 0.5 Is Jarring for Equal-Strength Regions — DESIGN NOTE

**Location:** `UTimeWeatherSubsystem::GetBlendedWeatherState`

Tag blending uses winner-takes-all at the 0.5 threshold. This is intentional and documented, but it means a player standing exactly on a region boundary flips between two tag sets on every tick if the area system supplies a value near 0.5.

**Recommendation:** The area system driving this should apply hysteresis (e.g. tag switches at 0.4 on the way out and 0.6 on the way in). Document this expectation clearly on the method. The fix belongs in the area system, not here.

---

## Issue 8: No Validation on `FSeasonWeatherEvent::WindowStart < WindowEnd` in Editor — MINOR

**Location:** `FSeasonWeatherEvent` (no `IsDataValid`)

`RollTimedEvents` guards: `if (SE.WindowEnd <= SE.WindowStart) continue;` — silently skips malformed entries. This should be caught at data validation time, not silently at runtime.

**Fix:** Add `IsDataValid` to `USeasonDefinition` that validates each `FSeasonWeatherEvent`:

```cpp
virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override
{
    EDataValidationResult Result = Super::IsDataValid(Context);
    if (!SeasonTag.IsValid())
    {
        Context.AddError(FText::FromString(TEXT("SeasonTag must be set.")));
        Result = EDataValidationResult::Invalid;
    }
    for (int32 i = 0; i < TimedEvents.Num(); ++i)
    {
        const FSeasonWeatherEvent& SE = TimedEvents[i];
        if (SE.WindowEnd <= SE.WindowStart)
        {
            Context.AddError(FText::FromString(
                FString::Printf(TEXT("TimedEvents[%d]: WindowEnd must be > WindowStart."), i)));
            Result = EDataValidationResult::Invalid;
        }
    }
    return Result;
}
```

---

## Issue 9: Registered Providers Are Raw Pointers Without Actor Lifecycle Guard — MODERATE

**Location:** `UTimeWeatherSubsystem::RegisteredProviders`

```cpp
TArray<IWeatherContextProvider*> RegisteredProviders;
```

The spec relies entirely on region actors calling `UnregisterContextProvider` in `EndPlay`. If an actor is destroyed without calling `EndPlay` (e.g. force-destroyed during world teardown, hot reload edge cases), the dangling pointer is never removed.

**Recommendation:** Store `TWeakInterfacePtr<IWeatherContextProvider>` or pair the interface pointer with a `TWeakObjectPtr<UObject>` for lifetime tracking. On each tick, sweep for invalid weak pointers and auto-unregister with a warning.

Alternatively, `RegisteredProviders` is only used to validate uniqueness (not iterated per-tick) — it could be removed entirely since `ContextStates` already keys uniquely by GUID. The only purpose `RegisteredProviders` serves is the `AddUnique` guard, which is redundant given the GUID check already prevents duplicates.

---

## Issue 10: `MakeSeededStream` Hash Chain Is Commutative — MINOR

**Location:** `UTimeWeatherSubsystem::MakeSeededStream`

```cpp
uint32 Seed = GetTypeHash(Config->WeatherSeedBase);
Seed = HashCombine(Seed, GetTypeHash(Day));
Seed = HashCombine(Seed, GetTypeHash(ContextId));
```

`HashCombine` is not commutative but is order-dependent. Swapping `Day` and `ContextId` in future maintenance would produce different seeds without any compile-time warning. The function is correct as written but fragile.

**Recommendation:** Comment the exact hash order as part of the stability contract. Changing it is a breaking change that reshuffles all weather globally.

---

## Issue 11: `UWeatherSequence` Objects Are Not CDO-Safe — MINOR

**Location:** `UWeatherSequence` hierarchy

Sequences are `EditInlineNew` UObjects on data assets. When the subsystem resolves a sequence via `ResolveSequence`, it may return the same object for multiple contexts (e.g. a global default sequence). Because `GetNextWeather` is called with a `FRandomStream&` (passed by ref from per-context state), the same sequence object being called concurrently for two contexts would be unsafe if it stores state.

The spec says "Never store mutable state" — but this is enforced only by convention, not by the class design.

**Recommendation:** Add a `checkf` or `ensure` in debug builds that sequences have no mutable member variables, or make the note in the contract more prominent so implementors don't add caching fields.

---

## Positive Notes

- **Deterministic seed design** is excellent. `Hash(SeedBase, Day, ContextId)` provides per-region, per-day uniqueness with zero persistence overhead.
- **Single-output `FWeatherBlendState`** is the right abstraction. VFX/audio systems can evolve independently.
- **No per-tick asset queries** — the daily schedule pre-computes everything. This is a correct and efficient pattern.
- **Overlay event fade-out on cancel** (proportional to current alpha) is a nice quality-of-life detail that prevents jarring cut-offs.
- **`ValidPredecessors` filter** on random sequences avoids the "sunny → blizzard" problem without requiring a full graph.
- **`IsDataValid` overrides** on all data assets catch authoring errors in the editor before runtime.
- **GMS-only broadcasts** with tag-level change gating (no alpha-only events) is the right pattern for a server-authoritative system.
