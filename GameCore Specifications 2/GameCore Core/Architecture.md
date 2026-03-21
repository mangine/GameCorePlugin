# GameCore Core — Architecture

**Part of:** GameCore Plugin | **Module:** `GameCore` | **UE Version:** 5.7

---

## Overview

GameCore Core is the **foundation layer** of the GameCore plugin. It provides generic, game-agnostic interfaces and utilities that sit at the bottom of the entire dependency graph. Every other GameCore system can depend on these without creating coupling to any higher-level feature system.

The two interfaces provided by this layer are:

- **`ISourceIDInterface`** — identifies any `UObject` as a structured, tag-labelled event source. Used by audit/logging paths across XP grants, drops, market events, and any future system that needs provenance tracking.
- **`IGroupProvider`** — a read-only contract for querying group membership state from a `APlayerState` (or any actor that owns it). Consumed by shared quest logic and any future system that scales with group size.
- **`UGroupProviderDelegates`** — an optional `UActorComponent` that provides a delegate-backed default `IGroupProvider` implementation, allowing decoupled wiring without subclassing.

These interfaces carry **no runtime logic** — they are pure contracts. All concrete behaviour lives in the implementing class, keeping the core layer thin and dependency-free.

---

## Dependencies

### Unreal Engine Modules

```csharp
// GameCore.Build.cs
PublicDependencyModuleNames.AddRange(new string[]
{
    "CoreUObject",
    "Engine",
    "GameplayTags",    // FGameplayTag used by ISourceIDInterface
});
```

### Runtime Dependencies

| Dependency | Type | Reason |
|---|---|---|
| `FGameplayTag` | UE built-in | Structured source identity in `ISourceIDInterface` |
| `APlayerState` | UE built-in | Forward-declared for `IGroupProvider::GetGroupMembers` |

### GameCore Plugin Systems

**None.** GameCore Core has zero intra-plugin dependencies. It is the absolute base layer.

---

## Requirements

| # | Requirement |
|---|---|
| R1 | Any `UObject` must be able to declare a structured source identity consumable by any system without coupling to that system |
| R2 | Source identity must use `FGameplayTag` for hierarchical classification, enabling log filtering and analytics bucketing |
| R3 | Any `APlayerState` must be able to expose group membership state through a stable interface without exposing its concrete party/group system |
| R4 | Group membership queries must be safe to call on both server and client |
| R5 | Group state reads must never allocate per-call heap memory for the common query path |
| R6 | All interfaces must have safe null/solo fallbacks so consuming systems work before any group or source system is wired up |
| R7 | Core interfaces must not carry game-specific semantics (no "crew", "pirate", "ship" concepts) |

---

## Features

- **Tag-based source identity** — `ISourceIDInterface::GetSourceTag()` returns a `FGameplayTag` from the `Source.*` hierarchy. Any system can log provenance without knowing the concrete source type.
- **Optional display name** — `GetSourceDisplayName()` has an empty default, so tooling-facing names are opt-in overhead only.
- **Read-only group contract** — `IGroupProvider` exposes four polling queries covering size, leadership, membership list, and coordinator actor location. No write path exists on the interface.
- **Delegate-based wiring** — `UGroupProviderDelegates` lets the game module bind group state from any system to `APlayerState`'s `IGroupProvider` without subclassing `APlayerState`.
- **Safe solo fallbacks** — all `UGroupProviderDelegates` methods return safe solo-player defaults (size = 1, leader = false, member list = self) when delegates are unbound.
- **No notification mechanism** — both interfaces are polling-only. Group change events belong to the concrete group system and should be broadcast via the Event Bus.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| `FGameplayTag` for source identity, not a string or enum | Hierarchical, editor-validated, zero-cost comparison, consistent with the rest of the plugin |
| `GetSourceDisplayName()` is non-pure with empty default | Avoids forcing all implementors to provide UI/debug strings; only override when tooling needs it |
| `IGroupProvider` is read-only | Consuming systems (quests) must never mutate group state — enforcement at the interface level prevents subtle authority bugs |
| `IGroupProvider` implemented on `APlayerState`, not on a component | `USharedQuestComponent` lives on `APlayerState` and can `Cast<IGroupProvider>(GetOwner())` without a secondary component lookup |
| `UGroupProviderDelegates` as an opt-in component, not required | Teams that implement group logic directly on `APlayerState` incur zero overhead from the delegate helper |
| `GetGroupMembers` uses `TArray<APlayerState*>& OutMembers` | Avoids per-call heap allocation for the majority of queries that go into an already-allocated local array |
| No notification mechanism on `IGroupProvider` | Notification responsibility belongs to the concrete group system; polling-only contract keeps Core dependency-free |

---

## Logic Flow

### Source Identity Path

```
Any System (XP, Drops, Market...)
  └─ Receives TScriptInterface<ISourceIDInterface> Source
       ├─ Source.GetObject() null-check
       ├─ Source->GetSourceTag()         → FGameplayTag (e.g. Source.Mob.Skeleton)
       └─ Source->GetSourceDisplayName() → FText (optional, for CS/debug tools)
            └─ Forward to backend telemetry / audit log
```

### Group State Query Path

```
USharedQuestComponent (or any consumer)
  └─ Cast<IGroupProvider>(GetOwner())
       ├─ nullptr → solo fallback (GroupSize = 1)
       └─ IGroupProvider* Provider
            ├─ Provider->GetGroupSize()         → int32
            ├─ Provider->IsGroupLeader()        → bool
            ├─ Provider->GetGroupMembers(Out)   → fills TArray<APlayerState*>
            └─ Provider->GetGroupActor()        → AActor* (coordinator, may be nullptr)
```

### Delegate Wiring Path (UGroupProviderDelegates)

```
Game Module Init (e.g. party component BeginPlay)
  └─ PlayerState->GroupProviderDelegates->GetGroupSizeDelegate.BindUObject(...)
  └─ PlayerState->GroupProviderDelegates->IsGroupLeaderDelegate.BindUObject(...)
  └─ PlayerState->GroupProviderDelegates->GetGroupMembersDelegate.BindUObject(...)
  └─ PlayerState->GroupProviderDelegates->GetGroupActorDelegate.BindUObject(...)

Runtime query:
  └─ APlayerState::GetGroupSize()
       └─ GroupProviderDelegates->ForwardGetGroupSize()
            ├─ Delegate bound   → Execute() → concrete party system value
            └─ Delegate unbound → return 1  (solo fallback)
```

---

## Known Issues

| # | Issue | Status |
|---|---|---|
| KI-1 | `IGroupProvider` has no change notification — consumers must poll | By design; notification belongs to the concrete group system via Event Bus |
| KI-2 | `GetGroupMembers` output is valid for the current frame only — callers must not cache the array across frames | Documented constraint; no GC safety for stale `APlayerState*` pointers |
| KI-3 | `UGroupProviderDelegates` delegates are C++ only (`TDelegate`) — not Blueprint-bindable | Acceptable; group wiring is always server-side C++ code |
| KI-4 | `ISourceIDInterface::GetSourceTag_Implementation` is pure virtual — Blueprint classes that implement the interface cannot satisfy it without a C++ intermediary | Acceptable; source objects are server-only C++ actors/components |

---

## File Structure

```
GameCore/Source/GameCore/
├── Core/
│   └── SourceID/
│       └── SourceIDInterface.h          — ISourceIDInterface (header-only)
└── Interfaces/
    └── GroupProvider.h                  — IGroupProvider + UGroupProviderDelegates (header-only)
```

Both files are **header-only** — no `.cpp` is required since all implementations are either pure-virtual, inline, or delegate-forwarded.
