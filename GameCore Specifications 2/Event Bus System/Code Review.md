# Event Bus System — Code Review

---

## Overview

The Event Bus System is well-structured overall. The layering of `UGameCoreEventBus` over GMS with scope enforcement is clean, and the `UGameCoreEventWatcher` multi-tag handle pattern is the right abstraction for reactive systems. The following documents issues, risks, and concrete improvement suggestions.

---

## Issues

### 1. Inconsistent `ClientOnly` Scope Evaluation

**Severity: Bug**

In the original `UGameCoreEventBus::PassesScopeGuard`, `ClientOnly` is evaluated as:
```cpp
case EGameCoreEventScope::ClientOnly: return World->GetNetMode() == NM_Client;
```

In `UGameCoreEventWatcher::PassesScopeCheck`, it is evaluated as:
```cpp
case EGameCoreEventScope::ClientOnly: return World->GetNetMode() == NM_Client
                                          || World->GetNetMode() == NM_Standalone;
```

The two implementations are inconsistent. The Scope Guard documentation says standalone is treated as server for `ServerOnly`, but `ClientOnly` behaviour on standalone is never defined in the original spec.

**Fix:** Align both implementations. Standalone should pass `ClientOnly` because standalone runs both roles and any client-side UI logic still needs to fire. This is the corrected behaviour in the new spec.

---

### 2. `Owner` Parameter in `StartListening` Is Unused

**Severity: Design gap**

Both `StartListening` overloads accept an `UObject* Owner` parameter, but the implementation never uses it — it is not passed to `GMS->RegisterListener`, not stored, and not used for any debug logging or lifecycle tracking.

```cpp
// Owner is accepted but silently ignored:
FGameplayMessageListenerHandle UGameCoreEventBus::StartListening(
    FGameplayTag Channel,
    UObject* Owner,   // ← never used
    TFunction<...> Callback)
```

This is misleading — callers may assume `Owner` provides some lifetime safety (similar to `AddDynamic` patterns), but it does not. Leaked handles result in dangling lambdas, which is the entire failure mode this parameter implies it prevents.

**Options:**
1. Remove the `Owner` parameter entirely and document that callers are fully responsible for handle lifetime.
2. Use `Owner` to auto-unregister via `FObjectKey` tracking (more complex — may not be worth it for a generic bus).

**Recommendation:** Remove `Owner` from `UGameCoreEventBus::StartListening`. The watcher already stores `OwnerDebugName` for debugging. The bus API should be minimal.

---

### 3. `FProgressionLevelUpMessage` and `FProgressionXPChangedMessage` Use `TObjectPtr` for Actors

**Severity: Correctness / lifetime risk**

Message structs are value types passed as `FInstancedStruct` payloads. They outlive the broadcast frame if any listener stores the payload. `TObjectPtr<APlayerState>` and `TObjectPtr<AActor>` in message structs create an implicit assumption that those actors remain valid across the listener's lifetime.

**Fix:** Change actor/component fields in message structs to `TWeakObjectPtr`. `FStateMachineStateChangedMessage` already does this correctly — the Progression messages should follow the same pattern.

---

### 4. No Validation That a Broadcast Tag Is a Known GameCore Channel

**Severity: Minor / authoring risk**

The bus validates `Channel.IsValid()` but not that it is a tag under the `GameCoreEvent.*` hierarchy. A caller can broadcast on any arbitrary tag (e.g. `SomeOtherSystem.Event`) without error.

This is low-severity in a controlled plugin codebase but creates risk as the system grows.

**Suggestion:** Add a non-shipping `ensureMsgf` that the channel tag has `GameCoreEvent` as a root (using `MatchesTag` against a cached root tag). Drop the check in shipping.

---

### 5. `UGameCoreEventWatcher::Register` with Empty `FGameplayTagContainer` Silently Returns Invalid Handle

**Severity: Minor**

```cpp
if (!Callback || Tags.IsEmpty()) return FEventWatchHandle{};
```

Silent failure on empty tags may be surprising. A caller might construct a `FGameplayTagContainer`, fail to add any tags due to an upstream bug, and then register silently with an invalid handle — leading to a no-op listener with no log output.

**Fix:** Add a non-shipping `ensureMsgf(!Tags.IsEmpty(), ...)` before the early return.

---

### 6. No Blueprint-Callable `Register` on `UGameCoreEventWatcher`

**Severity: Low / scope decision**

The watcher's `Register` takes a `TFunction` which is not Blueprint-accessible. The bus's `StartListening` is also C++-only. Blueprints cannot subscribe to the event bus at all.

For a server-side MMORPG plugin, this is likely fine — but worth acknowledging as a deliberate decision. If Blueprint systems (e.g. designer-authored quest scripting) need to react to bus events, an adapter layer would be required.

**Suggestion:** Document this as a deliberate scope decision in Architecture.md. If Blueprint access is ever needed, a `UGameCoreEventBusListenerComponent` could bridge the gap without modifying the core bus.

---

### 7. Watcher `Deinitialize` Iterates `BusHandles` While Calling `StopListening`

**Severity: Correctness risk**

```cpp
void UGameCoreEventWatcher::Deinitialize()
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        for (auto& Pair : BusHandles)
            Bus->StopListening(Pair.Value);  // StopListening resets the handle — but Pair.Value is in the map
    BusHandles.Empty();
    ...
}
```

`StopListening` resets the handle to `FGameplayMessageListenerHandle{}`. Since `Pair.Value` is a reference into `BusHandles`, the reset modifies the map entry in place while iterating. This is safe in practice because we empty the map immediately after — but the iteration order and mutation make this subtly brittle.

**Fix:** Collect handles into a local array first, then iterate:
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

The original spec flags this as a potential future feature. If hierarchical tag subscription is ever needed, the correct implementation is **at the bus level** — the bus intercepts the broadcast, inspects the tag, and fans out to all registered parent-tag listeners manually. GMS cannot do this natively. Document this in Architecture.md as a future extension path.

### S2. High-Frequency Channel Batching Should Be Enforced, Not Just Documented

`XPChanged` and `PointPoolChanged` include a written warning about batching. Consider adding a non-shipping rate-limiter per channel (e.g. a `TMap<FGameplayTag, double>` of last broadcast timestamps with a configurable minimum interval). This would catch accidental per-hit broadcasts during development. Can be stripped entirely in shipping.

### S3. `FEventWatchHandle` Should Be a `USTRUCT` If Blueprint Access Is Planned

Currently it is a plain C++ struct with no `GENERATED_BODY()`. If Blueprint access is ever added, it will need to become a `USTRUCT`. Note for future implementation.
