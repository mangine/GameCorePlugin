# Interaction System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Interaction System is a decoupled, tag-driven, priority-resolved interaction layer between world actors and players. It is game-agnostic, built for MMO-scale actor counts, minimal network traffic, and defensible against common cheat vectors. Tag-based filtering and data-driven requirements are used throughout — familiarity with the [Gameplay Tags System](Gameplay%20Tags%20System%20317d261a36cf80b989cce2476ac108a3.md) and the [Requirement System](Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md) is assumed.

---

# System Units

| Unit | Class / Interface | Lives On |
| --- | --- | --- |
| Interaction Host | `UInteractionComponent` | Any interactable Actor (max one per actor) |
| Interaction Scanner | `UInteractionManagerComponent` | Player Pawn (owning client only) |
| Tag Authority | `ITaggedInterface`  • `UGameplayTagComponent` | Any Actor needing tag state |
| Condition Authority | `URequirement_Composite` on `FInteractionEntryConfig` | Per entry (data asset or inline) |
| Icon Configuration | `UInteractionIconDataAsset` | Per `UInteractionComponent` (optional) |

---

# How the Pieces Connect

Understanding the data flow end-to-end is essential before reading any individual component specification.

---

# Presentation Features

Two opt-in presentation features extend the system without modifying its core data flow, replication, or server logic. Neither touches `UInteractionComponent` directly — both are additive.

## Actor Highlight — `UHighlightComponent`

An optional `USceneComponent` added to any actor that should visually indicate focus. When `UInteractionManagerComponent` selects a new best interactable, it calls `SetHighlightActive(true)` on the incoming actor's `UHighlightComponent` and `SetHighlightActive(false)` on the outgoing one. The query happens only on best-change events, not on every scan tick.

Uses **Custom Depth + Stencil**: one post-process pass covers all highlighted actors at constant GPU cost regardless of count. The configurable `StencilValue` allows the PP shader to color-code actor categories independently (NPCs, items, quest objectives, etc.). Fully inert on the server and non-owning clients.

Full specification: [UHighlightComponent](Interaction%20System/UHighlightComponent%2031bd261a36cf81ddbf13d1ae8077fcb0.md)

## Contextual UI Descriptors — `UInteractionUIDescriptor` + `UInteractionDescriptorSubsystem`

Descriptors bridge the gap between **static entry config** and **live actor data** for the contextual UI panel shown when an interactable is focused.

The data ownership split is intentional:

- `FInteractionEntryConfig` owns **action data** — `Label` (verb), `InputAction`, `EntryIconOverride`, `HoldTimeSeconds`. Static, designer-authored, identical on all machines, never replicated.
- `UInteractionUIDescriptor` owns **contextual presentation logic** — reads the live actor at focus time (NPC name, item stats, ship condition) and populates named widget slots. Never replicated. Stateless — the same instance is shared across all actors using that entry config.

`FInteractionEntryConfig` gains one field: `UIDescriptorClass` (`TSubclassOf<UInteractionUIDescriptor>`). `FResolvedInteractionOption` gains one field: `UIDescriptor` (resolved instance, nullable). The widget receives both via the existing `OnResolvedOptionsChanged` broadcast — no new delegates needed.

`UInteractionDescriptorSubsystem` caches one instance per descriptor class for the lifetime of the game session. Hundreds of actors sharing the same descriptor class cost exactly one `UObject` in memory.

Full specifications: [UInteractionUIDescriptor](Interaction%20System/UInteractionUIDescriptor%2031bd261a36cf81ffbb2fcd1d43d7970b.md) | [UInteractionDescriptorSubsystem](Interaction%20System/UInteractionDescriptorSubsystem%2031bd261a36cf81bd8e7dee50384145a3.md)

**Finding candidates.** The `UInteractionManagerComponent` lives on the player pawn and runs only on the owning client — it is fully inert on the server and non-owning clients. On a repeating timer (`ScanPeriod`, default 0.2s) it performs a sphere overlap on the **Interaction** collision channel. Any actor with a `UInteractionComponent` and a primitive on that channel is a candidate. Each candidate is scored by a weighted combination of distance and camera view angle, and the highest-scoring one becomes the **current best component**. The scanner binds to that component's `OnEntryStateChanged` delegate so any server-driven state change immediately triggers a re-resolve without waiting for the next scan tick.

**Resolving options.** Once a best component is selected, the scanner calls `ResolveOptions` on it, passing the player pawn as the source actor and the interactable as the target. The component iterates its entries — defined as `UInteractionEntryDataAsset` references or inline `FInteractionEntryConfig` structs — and evaluates each one in order: tag pre-filters via `ITaggedInterface` (cheap bitset AND), then the entry's optional `URequirement_Composite` evaluated client-side using locally cached replicated state (must be cheap). Each entry is assigned a final `EInteractableState`: `Available`, `Occupied`, or `Cooldown` come from replicated net state; `Locked` is evaluated client-side only and never sent over the wire. The configured `EResolveMode` (one winner per group, or all entries including locked ones) shapes the final output list of `FResolvedInteractionOption` structs. These are written into the scanner's pre-allocated `ResolvedBuffer` and broadcast via `OnResolvedOptionsChanged` for the UI widget to consume.

**Static config vs. replicated state.** Entry definitions (`FInteractionEntryConfig`) are identical on all machines and never replicated — the server and every client load them from the same DataAsset or inline array. Only runtime state (`EInteractableState`, `bServerEnabled`) travels over the wire, delta-compressed via `FFastArraySerializer` so only changed items are sent per cycle. This is the core bandwidth optimisation: hundreds of interactable actors can be in relevancy range with near-zero replication cost at steady state.

**Triggering an interaction.** When the player presses the interact input, the scanner fires `ServerRequestInteract` as a reliable RPC — for Press interactions immediately, for Hold interactions only on successful hold completion (the server never knows about hold progress). The server re-validates the request independently through an eight-step chain: rate limit → entry exists → admin-enabled → state → distance → source tags → target tags → entry requirements. On success it broadcasts `OnInteractionConfirmed` for game systems to bind to, then sends a reliable `ClientRPC_OnInteractionConfirmed` back to the instigator. On any failure it sends `ClientRPC_OnInteractionRejected` with an `EInteractionRejectionReason` for UI feedback. The server never scans, scores, or resolves — it only validates and executes.

**Hold interactions.** The scanner manages the hold state machine entirely on the client. When a Hold-type entry is triggered, Tick is enabled and `HoldAccumulatedTime` increases each frame. `OnHoldProgressChanged` fires every frame for the UI progress bar. The hold is cancelled automatically if the player releases input, moves beyond `HoldCancelMoveThreshold`, acquires a disabling tag, or the target component changes. The server RPC fires only on completion.

**Replication feeding back into the UI.** When game systems call `SetEntryState` or `SetEntryServerEnabled` on the server, the affected `FInteractionEntryNetState` item is marked dirty and `FFastArraySerializer` sends only that item to all relevant clients. On receipt, `PostReplicatedChange` fires `OnEntryStateChanged` on the component. If the scanner is currently tracking that component, its bound `OnTrackedStateChanged` handler fires `RunResolve`, which rebuilds the resolved option list and pushes it to the UI — without waiting for the next scan tick.

**Tag filtering.** Both source and target actors are queried for tags using `ITaggedInterface`. The scanner caches the pawn's `ITaggedInterface*` at `BeginPlay` to avoid repeated casts on the hot path. `FGameplayTagContainer` required-tag checks (bitset AND, near-zero cost) cover most cases. `FGameplayTagQuery` is available for complex AND/OR/NOT logic and is only evaluated when non-empty.

**Entry requirements.** Each `FInteractionEntryConfig` carries an optional `TArray<TObjectPtr<URequirement>> EntryRequirements`. The array is always AND — every element must pass. When a specific condition needs OR or NOT logic, a `URequirement_Composite` is added as one element of the array with the appropriate operator. On the client (during `ResolveOptions`), the array is evaluated via `URequirementLibrary::EvaluateAll` using locally cached replicated state to determine `Locked` status and surface a `FailureReason` for the UI. On the server (during `ServerRequestInteract`), the same array is re-evaluated authoritatively. Requirements must all be synchronous — detected at `BeginPlay` via `URequirementLibrary::ValidateRequirements`.

**Icon resolution.** The UI widget resolves the display icon for each option via a three-step priority chain: entry icon override (from `FInteractionEntryConfig`) → component's `UInteractionIconDataAsset` mapped by state → null. Null is a valid result — the widget hides the icon slot gracefully. No default asset is shipped; icons are entirely optional.

---

# Core Design Principles

These principles are the constraints everything else is built around. When in doubt about a design decision anywhere in the system, refer back here.

- **Immutable config is separated from mutable runtime state.** Entry definitions are DataAssets or inline structs — never modified at runtime. Only `EInteractableState` and `bServerEnabled` change and replicate.
- **Replication is minimal and delta-compressed.** Only state changes travel over the network. `FFastArraySerializer` sends only changed items per cycle. Config is never on the wire.
- **Resolution is always client-side.** The server only validates and executes. It never scans, scores, or resolves options.
- **No per-frame allocations on the scan path.** `OverlapBuffer` and `ResolvedBuffer` are pre-allocated at `BeginPlay`. Tick is enabled only during active hold interactions.
- **Game systems are never imported.** The interaction system extends exclusively via the Requirement System and tag interfaces — it never references the resource system, quest system, ability system, or any game-specific code directly.
- **Validation is layered.** Tag checks are the fast pre-filter (bitset AND, evaluated on both client and server). Entry requirements (`URequirement_Composite`) are the authoritative condition gate — evaluated client-side for display, server-side for authority.
- **UI configuration is optional and self-contained.** Each `UInteractionComponent` carries an optional `UInteractionIconDataAsset`. If unset, icon resolution returns null. The widget handles this gracefully.
- **One `UInteractionComponent` per actor.** The scanner finds one component per actor via `FindComponentByClass`. Multiple interaction options are expressed as entries within that single component, not as multiple components.
- **HISM instances are not Actors — do not extend this system to treat them as such.** See the HISM note in Future Work.

---

# Quick Setup

**Making an actor interactable:**

1. Add `UInteractionComponent` to the actor.
2. Add at least one entry — reference a `UInteractionEntryDataAsset` in `Entries`, or fill an `FInteractionEntryConfig` in `InlineEntries`.
3. Ensure the actor has a primitive component set to respond on the **Interaction** collision channel. The scanner's sphere overlap uses this channel exclusively — actors without it are invisible to the scanner.
4. Bind to `OnInteractionConfirmed` on the component (server-side) to execute the interaction.
5. Optionally implement `ITaggedInterface` if entries use tag gates. For non-GAS actors, attach `UGameplayTagComponent`. For GAS actors, forward through the `UAbilitySystemComponent`.
6. Optionally assign a `URequirement_Composite` to `FInteractionEntryConfig::EntryRequirements` for conditions beyond tag gates.

**Enabling interaction on a player pawn:**

1. Add `UInteractionManagerComponent` to the pawn.
2. Bind Enhanced Input actions to `RequestInteract(EntryIndex)` (on press) and `RequestInteractRelease()` (on release).
3. Bind `OnResolvedOptionsChanged` to drive the interaction prompt widget.
4. Bind `OnHoldProgressChanged` to drive the hold progress bar widget.
5. Optionally bind `OnInteractionRejected` to show server rejection feedback.

---

# Enums

All interaction system enums live in `InteractionEnums.h` — a single header included by nearly every other file in the system. Isolating them prevents cascade recompiles when values change.

`EInteractionInputType` — whether an entry requires a tap or a sustained hold. `EInteractableState` — the runtime visibility state of an entry as seen by the client. `Available`, `Occupied`, `Cooldown`, and `Disabled` are server-set and replicated; `Locked` is evaluated client-side only and never sent over the wire — it is the only state the client can impose unilaterally. `EInteractionRejectionReason` — the server's verdict returned to the client on RPC rejection, used for UI feedback. `EInteractionHoldState` — the scanner's internal hold state machine, never exposed to UI directly. `EResolveMode` — controls whether `ResolveOptions` outputs one winner per group (`Best`, for standard prompts) or all entries including locked ones (`All`, for inspect/examine UIs).

Full enum definitions are in the [Core Data Structures](Interaction%20System/Core%20Data%20Structures%20317d261a36cf80758e45dcfaa0a634ee.md) sub-page.

---

# Core Data Structures

The system's data layer is split into four concerns, each with a clear lifetime and ownership boundary.

**Static config** (`FInteractionEntryConfig`, `UInteractionEntryDataAsset`) — all immutable designer-authored data for a single interaction, including its optional `URequirement_Composite`. Lives identically on server and all clients. Never replicated. DataAsset entries are shared across multiple components — one asset, zero per-component duplication.

**Replicated runtime state** (`FInteractionEntryNetState`, `FInteractionEntryNetStateArray`) — the only data that travels over the wire. One item per entry, containing only `EInteractableState` and `bServerEnabled`. Delta-serialized via `FFastArraySerializer`. The `bServerEnabled` flag is kept separate from `State` intentionally — one is administrative control, the other is gameplay state.

**Client-side resolution output** (`FResolvedInteractionOption`) — produced by `ResolveOptions`, consumed by the UI. Holds a pointer (not a copy) into the config's `FText Label`, evaluated state, and requirement failure reason. Rebuilt on every re-resolve. Must not be cached across frames.

The **unified flat index** is the stable identifier used in all RPCs and net state items — DataAsset entries first, inline entries after. It must not change after `BeginPlay`.

Full definitions: [Core Data Structures](Interaction%20System/Core%20Data%20Structures%20317d261a36cf80758e45dcfaa0a634ee.md)

---

# Entry Requirements — `TArray<URequirement*>`

Each `FInteractionEntryConfig` carries an optional `TArray<TObjectPtr<URequirement>> EntryRequirements` (marked `Instanced`). This is the condition gate for the entry beyond tag filters. It replaces the former `IInteractionConditionProvider` interface.

The array is evaluated at two call sites with different authority:

**Client-side (`ResolveOptions`)** — evaluated via `URequirementLibrary::EvaluateAll` using locally cached, replicated state only. Requirements that cannot be evaluated from local state must return `Pass` (optimistic display). A failed array marks the entry `Locked` and populates `FResolvedInteractionOption::ConditionLabel` from `FRequirementResult::FailureReason` for UI feedback.

**Server-side (`ServerRequestInteract`)** — evaluated authoritatively via `URequirementLibrary::EvaluateAll`. The `FRequirementContext` is constructed server-side from the RPC connection — never from client-provided data. A failed array rejects the request with `EInteractionRejectionReason::ConditionFailed` and sends the `FailureReason` text to the client via `ClientRPC_OnInteractionRejected`.

**The array is always AND.** Every element must pass. For OR or NOT logic on a specific condition, add a `URequirement_Composite` as one element of the array with the appropriate operator. Designers pick `URequirement_Composite` from the class picker in the Details panel and configure its children inline — no C++ required.

**Requirements on this field must be synchronous.** Detected at `BeginPlay` via `URequirementLibrary::ValidateRequirements` with `bRequireSync = true`. Logged as an error in development builds.

**Designers author conditions directly in the Data Asset.** Each slot in the `EntryRequirements` array has a class picker in the Details panel showing all loaded `URequirement` subclasses. Simple conditions (tag check, level gate) are added directly. Complex AND/OR/NOT trees use `URequirement_Composite` as the slot type.

> **Why `TArray<URequirement*>` and not a single `URequirement_Composite` root?** The array is the canonical pattern across all consuming systems (quests, abilities, crafting). A flat AND list is the common case — adding a composite root for every entry that only needs AND would be unnecessary overhead for designers. When OR or NOT is needed, one composite element handles it. This keeps the simple case simple and the complex case possible.

> **`FRequirementContext` construction.** On the client, `Context.Instigator` is the local pawn and `Context.PlayerState` is the local PlayerState. On the server, both are derived from the RPC connection — the server never trusts client-provided subject references. `Context.World` is `GetWorld()` in both cases.

---

# `UInteractionComponent`

The **interaction host** — added to any actor that can be interacted with, maximum one per actor. It owns the entry definitions (DataAsset references and inline configs), the replicated runtime state array, and the server-side request validation and confirmation logic.

Game systems interact with this component in two directions. Outbound: bind to `OnInteractionConfirmed` (server-side) to execute the interaction when the server approves a request. Inbound: call `SetEntryState` to reflect gameplay conditions (occupied, on cooldown, available), and `SetEntryServerEnabled` for administrative enable/disable independent of gameplay state.

Full specification: [UInteractionComponent](Interaction%20System/UInteractionComponent%20317d261a36cf8005b0f5c31343f4a4ef.md)

---

# `UInteractionManagerComponent`

The **client-side scanner** — added to the player pawn. Fully inert on the server and non-owning clients. On a repeating timer it finds the best nearby `UInteractionComponent`, resolves its entries into a prompt-ready list, and publishes the result for UI consumption.

The scanner owns the hold interaction state machine. Hold progress accumulates in Tick (enabled only during an active hold — zero overhead otherwise) and `OnHoldProgressChanged` fires every frame for the progress bar widget. The server RPC fires only on press or hold completion, never during accumulation. The scanner also handles `DisablingTags` suppression — when the pawn carries any tag in that container, the scanner clears its current best and stops resolving entirely, without waiting for the next tick.

The `ResolveMode` property (`Best` or `All`) controls the shape of the resolved output. `Best` (default) surfaces one winner per interaction group — the right choice for standard HUD prompts. `All` surfaces every entry including locked ones with their condition labels — useful for an inspect or examine UI that shows everything an actor offers. Debug visualisation (`gc.Interaction.Debug` CVar) is part of this component.

Full specification: [UInteractionManagerComponent](Interaction%20System/UInteractionManagerComponent%20317d261a36cf80449db1cb441d014b5a.md)

---

# `UInteractionIconDataAsset`

A plain `UDataAsset` subclass that maps each `EInteractableState` to a soft-referenced `UTexture2D`. Referenced optionally per `UInteractionComponent` — multiple components can share one asset. Icons load on demand when a widget first requests them; there is no loading cost at component or world initialisation.

The icon resolution order is: entry icon override (`FInteractionEntryConfig::EntryIconOverride`) → `IconDataAsset->GetIconForState(State)` → null. Null is a valid terminal result — the widget hides the icon slot. No default asset is shipped by the plugin.

Full specification: [UInteractionIconDataAsset](Interaction%20System/UInteractionIconDataAsset%20317d261a36cf806ab21ac22db3fbe473.md)

---

# Network Flow Summary

```jsx
── Owning Client ────────────────────────────────────────────────────────────────

  Timer (ScanPeriod):
    SphereOverlap → score candidates → select best UInteractionComponent
    On candidate change:
      Unbind OnEntryStateChanged from previous component
      Bind  OnEntryStateChanged to new component
      Fire OnBestInteractableChanged → RunResolve → OnResolvedOptionsChanged → UI

  State change on tracked component (OnTrackedStateChanged):
    RunResolve → OnResolvedOptionsChanged → UI  (no scan — direct re-resolve)

  DisablingTag acquired:
    ClearCurrentBest → OnBestInteractableChanged(prev, null) → UI clears prompt

  Player input — Press:
    RequestInteract(EntryIndex)
    Client pre-check: bServerEnabled && State != Disabled
    ServerRequestInteract(EntryIndex)  [Reliable RPC → server]
    TriggerImmediateRescan()

  Player input — Hold start:
    RequestInteract(EntryIndex) → BeginHold()
    Tick enabled → HoldAccumulatedTime increases each frame
    OnHoldProgressChanged fires every frame [0..1] → UI progress bar

  Hold cancelled (input release / player moved / disabling tag / target lost):
    CancelHold() → Tick disabled → OnHoldCancelled(EntryIndex)

  Hold completed (progress == 1.0):
    CompleteHold()
    ServerRequestInteract(EntryIndex)  [Reliable RPC → server — fires only on completion]
    TriggerImmediateRescan()

── Server ───────────────────────────────────────────────────────────────────────

  ServerRequestInteract(EntryIndex):
    PC / Pawn derived from RPC connection — never passed as a parameter
    [1] Rate limit (0.3s cooldown per PC)              → reject RateLimited
    [2] Entry exists                                    → reject EntryNotFound
    [3] bServerEnabled                                  → reject EntryUnavailable
    [4] State (Occupied / Cooldown / Disabled)          → reject EntryUnavailable
    [5] Distance + 75cm latency tolerance               → reject OutOfRange
    [6] Source tag checks (Required + Query)            → reject TagMismatch
    [7] Target tag checks (Required + Query)            → reject TagMismatch
    [8] EntryRequirements (TArray<URequirement*>)      → reject ConditionFailed

    ❌ ClientRPC_OnInteractionRejected(EntryIndex, Reason)   [Reliable → instigator only]

    ✅ OnInteractionConfirmed.Broadcast(Pawn, EntryIndex)    [server-side — game systems act here]
       ClientRPC_OnInteractionConfirmed(EntryIndex)          [Reliable → instigator only]

── Replication ──────────────────────────────────────────────────────────────────

  SetEntryState / SetEntryServerEnabled on server:
    NetStates item marked dirty (Push Model)
    FFastArraySerializer → sends only changed items to relevant clients
    PostReplicatedChange → OnEntryStateChanged.Broadcast(Component, EntryIndex)
    Scanner's OnTrackedStateChanged → RunResolve → OnResolvedOptionsChanged → UI
```

---

# Implementation Constraints

- **Scanner never runs on the server.** `BeginPlay` exits early unless `IsLocallyControlled()`. Inert on all non-owning machines.
- **`FInteractionEntryConfig` is never replicated.** Must be identical on all machines at all times. Never modify at runtime.
- **Entry array size is frozen after `BeginPlay`.** The unified flat index is a stable contract. Use `SetEntryServerEnabled(false)` or `SetEntryState(Disabled)` to hide entries dynamically without resizing.
- **`EntryRequirements` must contain only synchronous requirements.** Detected at `BeginPlay` via `URequirementLibrary::ValidateRequirements` with `bRequireSync = true`. Async requirements must not appear on interaction entries, including inside nested `URequirement_Composite` children.
- **Requirements on the client must only read locally cached, replicated state.** They must not perform subsystem lookups that depend on server-only data. Requirements that cannot satisfy this must return `Pass` on the client (optimistic display) — the server evaluation is the authority.
- **`CachedTagInterface` in the scanner is set once at `BeginPlay`.** If the pawn's `ITaggedInterface` implementation changes at runtime, the cache must be manually invalidated and refreshed.
- **Concurrency and session management belong to game systems.** The interaction system has no internal tracking of who is using what. Game systems signal back via `SetEntryState` and `SetEntryServerEnabled`.
- **Hold execution belongs to game systems.** The interaction system is a proxy and trigger only. Animation, sound, world state changes, and ability activation are the receiving system's responsibility.
- **GAS actors** implement `ITaggedInterface` by forwarding to their `UAbilitySystemComponent`. The interaction system never imports GAS headers.
- **Icons are optional.** `IconDataAsset` may be null on any component. Widgets must handle null icon results gracefully — no crash, no broken reference.
- **One `UInteractionComponent` per actor.** Adding a second instance to the same actor is unsupported and flagged as an editor error. Use multiple entries to express multiple interaction options.
- **HISM instances are not Actors and cannot host a `UInteractionComponent` directly.** Do not attempt to subclass or extend this system to handle HISM internally — see the HISM note in Future Work for the correct approach.

---

# Known Limitations

**Distance check uses `GetInteractionLocation()` on both client and server.** The client scan measures from the hit collider's location (via `Hit.Component->GetComponentLocation()`), while the server measures from `ComponentRef->GetInteractionLocation()` which defaults to the actor pivot. For most actors the interaction collider is placed at or near the pivot and these agree. If they diverge significantly, override `GetInteractionLocation()` on the component to return the correct world position. The 75cm server-side tolerance absorbs normal latency desync — it is not a substitute for correct placement.

**Requirements on the client must not depend on server-only state.** Requirements that gate on data not present in replicated state will produce optimistic `Pass` on the client and a server rejection the player cannot predict. Design requirement types used in interaction entries to be fully evaluable from locally available replicated data.

---

# File and Folder Structure

```jsx
GameCore/
├── GameCore.uplugin
│
├── Source/
│   ├── GameCore/                                      ← Runtime module
│   │   ├── GameCore.Build.cs
│   │   │
│   │   ├── Interaction/
│   │   │   ├── Components/
│   │   │   │   ├── InteractionComponent.h / .cpp
│   │   │   │   ├── InteractionScannerComponent.h / .cpp
│   │   │   │   └── HighlightComponent.h / .cpp            ← Opt-in focus highlight
│   │   │   ├── Data/
│   │   │   │   ├── InteractionEntryConfig.h           ← FInteractionEntryConfig struct
│   │   │   │   ├── InteractionEntryDataAsset.h / .cpp
│   │   │   │   └── InteractionNetState.h              ← FInteractionEntryNetState + Array
│   │   │   ├── Enums/
│   │   │   │   └── InteractionEnums.h                 ← All enums in one header
│   │   │   └── ResolvedInteractionOption.h            ← FResolvedInteractionOption struct
│   │   │
│   │   ├── Tags/                                      ← ITaggedInterface, UGameplayTagComponent
│   │   │   └── (see Gameplay Tags System specification)
│   │   │
│   │   ├── Requirements/                              ← URequirement, URequirement_Composite
│   │   │   └── (see Requirement System specification)
│   │   │
│   │   └── UI/
│   │       └── Interaction/
│   │           ├── InteractionIconDataAsset.h / .cpp
│   │           ├── InteractionUIDescriptor.h / .cpp       ← Abstract descriptor base
│   │           └── InteractionDescriptorSubsystem.h / .cpp ← Shared descriptor cache
│   │
│   └── GameCoreEditor/                                ← Editor-only module (not in packaged builds)
│       ├── GameCoreEditor.Build.cs
│       └── Interaction/
│           └── InteractionComponentDetails.h / .cpp
│
└── Content/
    └── UI/
        └── Icons/
            └── (project-provided UInteractionIconDataAsset assets — no plugin defaults)
```

**Key decisions:**

**`IInteractionConditionProvider` is removed.** Conditions are now data-driven via `URequirement_Composite` on `FInteractionEntryConfig`. No actor needs to implement an interface to gate interactions — designers author conditions directly in the asset.

**`Interaction/Interfaces/` folder is removed.** It previously held `InteractionConditionProvider.h / .cpp` only. With that interface gone, the folder has no content.

**`InteractionConditionResult.h` is removed.** `FInteractionConditionResult` is no longer needed — its pass/fail and failure reason are now carried by `FRequirementResult`, and its icon override field is superseded by `FInteractionEntryConfig::EntryIconOverride`.

**Enums in a dedicated header.** `InteractionEnums.h` is included by nearly every other file. Isolating it means changing an enum value doesn't cascade-recompile component headers.

**`Tags/` and `Requirements/` are listed but not owned here.** Both are defined in their respective system specifications and consumed by this system. Not duplicated here.

**`GameCoreEditor` is a separate module.** Validation, custom details panels, and Slate tooling must not be loaded in packaged builds.

---

# Future Work

**HISM Support — P1, deferred.** `UInteractionComponent` is designed for standard Actors. HISM instances are not Actors — they share a single `UHierarchicalInstancedStaticMeshComponent` and have no individual identity the interaction system can address.

**Do not attempt to handle HISM by subclassing `UInteractionComponent` or extending the scanner.** Integrating HISM awareness into this system would require changing the scanner's scoring interface, the distance check signature, and the server validation path — breaking the generic Actor abstraction for all other users and coupling the interaction system to a rendering strategy it has no business knowing about.

**The correct approach is a dedicated HISM Proxy Actor system, external to GameCore's interaction system.** That system's sole responsibility is to maintain a pool of lightweight proxy Actors — each a standard Actor with a `UInteractionComponent` — positioned at HISM instance locations within player relevancy range. From the interaction system's perspective, these proxy Actors are indistinguishable from any other interactable Actor. No scanner changes. No validation changes. No new base classes.

Key design points for the proxy system (to be specified separately):

- **Proximity-driven pool activation.** Proxies are activated for instances within player range and returned to the pool when instances leave range. The pool is pre-allocated at world init — no runtime Actor spawning on the hot path.
- **Instance eligibility is decided externally via a filter delegate.** The proxy system exposes a bindable delegate — "should this instance index receive a proxy?" — that any external system (harvesting, resource, fishing) can bind to. The proxy system itself has no knowledge of game-specific instance state.
- **Per-instance state (harvested, on cooldown, etc.) belongs to the owning game system**, not to the proxy or the interaction system. When an instance becomes non-interactable, the owning system calls `SetEntryServerEnabled(false)` on the proxy's `UInteractionComponent` — the same API used on any regular interactable Actor.
- **The pool lives on the HISM Actor** via a `UHISMInteractionBridgeComponent`. One bridge component per HISM Actor, one pool per bridge. This keeps lifecycle tied to the HISM Actor and makes the setup visible and debuggable in the editor.

[Core Data Structures](Interaction%20System/Core%20Data%20Structures%20317d261a36cf80758e45dcfaa0a634ee.md)

[`UInteractionComponent`](Interaction%20System/UInteractionComponent%20317d261a36cf8005b0f5c31343f4a4ef.md)

[`UInteractionManagerComponent`](Interaction%20System/UInteractionManagerComponent%20317d261a36cf80449db1cb441d014b5a.md)

[**`UInteractionIconDataAsset`**](Interaction%20System/UInteractionIconDataAsset%20317d261a36cf806ab21ac22db3fbe473.md)

[UHighlightComponent](Interaction%20System/UHighlightComponent%2031bd261a36cf81ddbf13d1ae8077fcb0.md)

[UInteractionUIDescriptor](Interaction%20System/UInteractionUIDescriptor%2031bd261a36cf81ffbb2fcd1d43d7970b.md)

[UInteractionDescriptorSubsystem](Interaction%20System/UInteractionDescriptorSubsystem%2031bd261a36cf81bd8e7dee50384145a3.md)
