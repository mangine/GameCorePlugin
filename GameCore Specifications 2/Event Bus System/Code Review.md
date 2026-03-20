# Event Bus System — Code Review

---

## Overview

The Event Bus System is well-structured overall. The layering of `UGameCoreEventBus` over GMS with scope enforcement is clean, and the `UGameCoreEventWatcher` multi-tag handle pattern is the right abstraction for reactive systems. The following documents remaining issues, risks, and concrete improvement suggestions.

---

## Issues

### 1. No Validation That a Broadcast Tag Is a Known GameCore Channel

**Severity: Minor / authoring risk**

The bus validates `Channel.IsValid()` but not that it is a tag under the `GameCoreEvent.*` hierarchy. A caller can broadcast on any arbitrary tag (e.g. `SomeOtherSystem.Event`) without error.

This is low-severity in a controlled plugin codebase but creates risk as the system grows.

**Suggestion:** Add a non-shipping `ensureMsgf` that the channel tag has `GameCoreEvent` as a root (using `MatchesTag` against a cached root tag). Drop the check in shipping.

---

### 2. `UGameCoreEventWatcher::Register` with Empty `FGameplayTagContainer` Silently Returns Invalid Handle

**Severity: Minor**

```cpp
if (!Callback || Tags.IsEmpty()) return FEventWatchHandle{};
```

Silent failure on empty tags may be surprising. A caller might construct a `FGameplayTagContainer`, fail to add any tags due to an upstream bug, and then register silently with an invalid handle — leading to a no-op listener with no log output.

**Fix:** Add a non-shipping `ensureMsgf(!Tags.IsEmpty(), ...)` before the early return.

---

### 3. No Blueprint-Callable `Register` on `UGameCoreEventWatcher`

**Severity: Low / scope decision**

The watcher's `Register` takes a `TFunction` which is not Blueprint-accessible. The bus's `StartListening` is also C++-only. Blueprints cannot subscribe to the event bus at all.

For a server-side MMORPG plugin, this is likely fine — but worth acknowledging as a deliberate decision. If Blueprint systems (e.g. designer-authored quest scripting) need to react to bus events, an adapter layer would be required.

**Suggestion:** If Blueprint access is ever needed, a `UGameCoreEventBusListenerComponent` could bridge the gap without modifying the core bus.

---

### 4. Watcher `Deinitialize` Iterates `BusHandles` While Calling `StopListening`

**Severity: Correctness risk**

```cpp
void UGameCoreEventWatcher::Deinitialize()
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        for (auto& Pair : BusHandles)
            Bus->StopListening(Pair.Value);  // resets handle in-place while iterating the map
    BusHandles.Empty();
    ...
}
```

`StopListening` resets `Pair.Value` to `FGameplayMessageListenerHandle{}` in-place while the loop is still iterating the map. This is safe in practice because the map is emptied immediately after, but the mutation-during-iteration is subtly brittle.

**Fix:** Collect handles into a local array first, then unregister:
```cpp
TArray<FGameplayMessageListenerHandle> HandlesToStop;
BusHandles.GenerateValueArray(HandlesToStop);
BusHandles.Empty();
for (auto& H : HandlesToStop)
    Bus->StopListening(H);
```

---

## Suggestions

### S1. Add a `StartListeningToTagHierarchy` Future Path

If hierarchical tag subscription is ever needed, the correct implementation is **at the bus level** — the bus intercepts the broadcast, inspects the tag, and fans out to all registered parent-tag listeners manually. GMS cannot do this natively. Document this in Architecture.md as a future extension path.

### S2. High-Frequency Channel Batching Should Be Enforced, Not Just Documented

`XPChanged` and `PointPoolChanged` include a written warning about batching. Consider adding a non-shipping rate-limiter per channel (e.g. a `TMap<FGameplayTag, double>` of last broadcast timestamps with a configurable minimum interval). This would catch accidental per-hit broadcasts during development. Stripped entirely in shipping.

### S3. `FEventWatchHandle` Should Be a `USTRUCT` If Blueprint Access Is Planned

Currently it is a plain C++ struct with no `GENERATED_BODY()`. If Blueprint access is ever added, it will need to become a `USTRUCT`. Note for future implementation.
