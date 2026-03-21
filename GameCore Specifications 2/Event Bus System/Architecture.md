# Event Bus System — Architecture

**Part of:** GameCore Plugin | **Module:** `GameCore` | **UE Version:** 5.7

---

## Overview

The Event Bus System provides **decoupled, world-scoped, cross-system event communication** via two complementary subsystems:

- **`UGameCoreEventBus`** — the broadcast layer. Wraps `UGameplayMessageSubsystem` (GMS) with scope enforcement, validation, and a typed API. All cross-system events go through here.
- **`UGameCoreEventWatcher`** — a subscription-routing layer on top of the bus. Any system registers one or more tag callbacks under a single handle. Manages bus listener lifetime automatically.

Delegates remain the correct tool for intra-system (same component/class) wiring. The bus is strictly for cross-system communication where broadcaster and listener must have zero knowledge of each other.

---

## Dependencies

### Unreal Engine Modules

```csharp
// GameCore.Build.cs
PublicDependencyModuleNames.AddRange(new string[]
{
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "GameplayMessageRuntime",  // UGameplayMessageSubsystem
    "StructUtils"              // FInstancedStruct
});
```

### Runtime Dependencies

| Dependency | Type | Reason |
|---|---|---|
| `UGameplayMessageSubsystem` | UE built-in (GMS) | Underlying broadcast transport |
| `FInstancedStruct` | UE built-in (StructUtils) | Generic payload wrapper |
| `FGameplayTag` | UE built-in | Channel identity |

### GameCore Plugin Systems

**None.** The Event Bus System has zero GameCore inter-system dependencies. It is the foundation layer that other systems broadcast through.

---

## Requirements

| # | Requirement |
|---|---|
| R1 | Any system must be able to broadcast a typed event payload without knowing who listens |
| R2 | Any system must be able to subscribe to one or more event channels without knowing who broadcasts |
| R3 | Broadcasts must be guarded so server-side events never fire on clients and vice-versa |
| R4 | Listener handles must be deterministic — no leaked subscriptions on teardown |
| R5 | Subscribing to multiple channels must be possible under a single revocable handle |
| R6 | The system must carry no domain knowledge — no game-specific logic inside bus or watcher |
| R7 | Tag matching must be exact (GMS constraint — no parent-tag fan-out) |
| R8 | The broadcast API must support both typed (internal) and raw `FInstancedStruct` (external) call sites |

---

## Features

- **Scope-guarded broadcast** — `EGameCoreEventScope` enum controls server/client/both execution. Enforced at `Broadcast` time and again at `UGameCoreEventWatcher` delivery time.
- **Typed listener API** — `StartListening<T>` unwraps `FInstancedStruct` → `T` internally, with a non-shipping `ensureMsgf` on type mismatch.
- **Raw listener API** — `StartListening(FInstancedStruct)` for callers that need runtime type inspection.
- **Watcher handle** — `FEventWatchHandle` covers N tags under one `Unregister` call.
- **Lazy bus subscription** — Watcher subscribes to a tag on the bus only when the first caller registers for it, and unsubscribes when the last does.
- **Re-entrant dispatch** — Watcher copies the active handle list before dispatching so `Register`/`Unregister` inside a callback is safe (effective from next broadcast).
- **Native tag cache** — `GameCoreEventTags` namespace caches `FGameplayTag` handles at module startup. Zero-cost broadcast-site lookup.
- **No-cache accessor** — `::Get(this)` static pattern; subsystem pointers are never stored as member fields.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| Wrap GMS, never expose it directly | Enforces scope guard, validation, and a single controlled API surface |
| `FInstancedStruct` as universal payload | Avoids UObject overhead, stack-allocated, type-safe via `GetPtr<T>()` |
| Scope defaults to `ServerOnly` | Conservative default — omitting scope on a broadcast never fires on a client accidentally |
| Watcher as separate subsystem | Decouples multi-tag subscription management from the broadcast primitive |
| Typed `Broadcast<T>` is internal-only | External call sites use `FInstancedStruct::Make` explicitly — keeps broadcast sites readable and intentional |
| Scope enforced twice (bus + watcher) | Watcher adds per-registration enforcement so a `Both`-scoped broadcast cannot trigger a `ServerOnly` registration on a client |
| No parent-tag subscription | GMS constraint — documented as a known limitation, not a design gap |
| Channel tags defined per-module | Avoids central tag file coupling; owning module registers its own tags |

---

## Logic Flow

### Broadcast Path

```
Caller
  └─ UGameCoreEventBus::Broadcast(Channel, FInstancedStruct, Scope)
       ├─ PassesScopeGuard(Scope)        → DROP if wrong machine
       ├─ ensureMsgf(Channel.IsValid())  → DROP + assert
       ├─ ensureMsgf(Message.IsValid())  → DROP + assert
       └─ GMS::BroadcastMessage<FInstancedStruct>(Channel, Message)
            └─ [synchronous] fires all GMS listeners for Channel
                  └─ UGameCoreEventWatcher::OnBusEvent(Tag, Payload)
                       ├─ copy TagToHandles[Tag] → IdsCopy
                       └─ for each Id in IdsCopy:
                            ├─ Entry = Entries[Id]
                            ├─ PassesScopeCheck(Entry.Scope)  → SKIP if mismatch
                            └─ Entry.Callback(Tag, Payload)
```

### Subscription Path

```
Caller
  └─ UGameCoreEventWatcher::Register(Owner, Tag(s), Scope, Callback)
       ├─ Allocate FEventWatchHandle (NextHandleId++)
       ├─ Store FWatchEntry in Entries[Handle.Id]
       ├─ Add Handle.Id to TagToHandles[Tag] for each tag
       └─ SubscribeTagIfNeeded(Tag)  →  if first for this tag:
              └─ UGameCoreEventBus::StartListening(Tag, this, OnBusEvent)
                   └─ GMS::RegisterListener<FInstancedStruct>(Tag, lambda)
```

### Unregister Path

```
Caller
  └─ UGameCoreEventWatcher::Unregister(Handle)
       ├─ Find FWatchEntry for Handle.Id
       ├─ For each tag in Entry.Tags:
       │    ├─ TagToHandles[Tag].Remove(Handle.Id)
       │    └─ if TagToHandles[Tag] is now empty:
       │         └─ UnsubscribeTagIfEmpty(Tag)
       │              └─ UGameCoreEventBus::StopListening(BusHandles[Tag])
       └─ Entries.Remove(Handle.Id)
```

---

## Known Issues

| # | Issue | Status |
|---|---|---|
| KI-1 | GMS is synchronous — heavy callbacks block the broadcast caller | By design; callers must defer heavy work internally |
| KI-2 | No parent-tag subscription — exact match only | GMS constraint; use leaf tags explicitly |
| KI-3 | No built-in event batching/throttling | High-frequency channels (XPChanged) must batch at call site |
| KI-4 | No cross-world channels — bus and watcher are world-scoped | By design; worlds are isolated |
| KI-5 | Scope is not a replication primitive — no data is sent over the network | By design; scope is a guard, not a transport |
| KI-6 | `ClientOnly` broadcasts require data already present on the client via replication | By design; bus is notification-only |
| KI-7 | `Register` inside a callback takes effect from the next broadcast only | Safe by design (ID copy before dispatch) |

---

## File Structure

```
GameCore/Source/GameCore/
└── EventBus/
    ├── GameCoreEventBus.h        — UGameCoreEventBus, EGameCoreEventScope, template impls
    ├── GameCoreEventBus.cpp      — Initialize, Deinitialize, Broadcast, StartListening, StopListening, PassesScopeGuard
    ├── GameCoreEventWatcher.h    — UGameCoreEventWatcher, FEventWatchHandle
    ├── GameCoreEventWatcher.cpp  — Register, Unregister, OnBusEvent, lazy sub/unsub, Deinitialize
    ├── GameCoreEventTags.h       — native FGameplayTag handle declarations (GameCoreEventTags namespace)
    └── GameCoreEventTags.cpp     — AddNativeGameplayTag registration in StartupModule
```

Message structs (`FProgressionLevelUpMessage`, `FStateMachineStateChangedMessage`, etc.) live **in the header of the owning system**, not inside the EventBus folder. See [GameCore Event Messages](GameCore%20Event%20Messages.md).
