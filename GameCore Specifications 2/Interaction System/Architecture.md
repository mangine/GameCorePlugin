# Interaction System — Architecture

**Plugin:** GameCore | **Module:** GameCore (Runtime) + GameCoreEditor | **UE:** 5.7 | **Status:** Active Specification

The Interaction System is a **tag-driven, priority-resolved, server-validated** interaction layer between world actors and pawns. It is game-agnostic, designed for MMO-scale actor counts, minimal replication traffic, and hardened against common cheat vectors. Game-specific logic is never imported — the system extends exclusively through the Requirement System and `ITaggedInterface`.

---

## Dependencies

### Unreal Engine Modules

| Module | Usage |
|---|---|
| `Engine` | `UActorComponent`, `USceneComponent`, `UDataAsset`, `UPrimaryDataAsset`, `AActor`, `APawn` |
| `GameplayTags` | `FGameplayTag`, `FGameplayTagContainer`, `FGameplayTagQuery` |
| `EnhancedInput` | `UInputAction` soft reference in `FInteractionEntryConfig` |
| `NetCore` | Push Model (`DOREPLIFETIME_WITH_PARAMS_FAST`, `MarkItemDirty`) |
| `UMG` | `UUserWidget` base for `UInteractionContextWidget` (game-side) |

### GameCore Systems

| System | Dependency Type | Usage |
|---|---|---|
| **Requirement System** | Hard | `URequirementList`, `URequirement`, `FRequirementContext`, `FRequirementResult`, `URequirementLibrary` — entry condition gates |
| **Gameplay Tags System** | Hard | `ITaggedInterface` — tag-based source/target filtering |
| **Event Bus System** | None (indirect) | Requirement System internally uses it for reactive watch; Interaction System never calls it directly |
| **Serialization System** | None | Interaction state is ephemeral — not persisted |
| **Backend System** | None | No audit or logging requirements at this layer |

> **Key decoupling principle:** The Interaction System never imports game-specific systems (resource, quest, shop, dialogue, GAS). It exposes delegates. Binding is the game module's responsibility.

---

## Requirements

### Functional
- Any actor can be made interactable by adding `UInteractionComponent` (max one per actor).
- A player pawn initiates interactions via `UInteractionManagerComponent`.
- Interaction entries are data-driven (`UInteractionEntryDataAsset` or inline `FInteractionEntryConfig`).
- Entries are gated by tag filters and optional `URequirementList` conditions.
- The server validates all interaction requests authoritatively before execution.
- Hold interactions accumulate progress client-side; the server RPC fires only on completion.
- Replicated state is minimal and delta-compressed — only state changes travel the wire.
- UI receives resolved options and hold progress via delegates; no UI state lives in game components.

### Non-Functional
- Zero per-frame heap allocations on the scan path.
- Tick enabled only during active hold interactions.
- Push Model replication — no per-tick dirty checks at steady state.
- Scanner is fully inert on server and non-owning clients.
- Maximum 255 entries per `UInteractionComponent` (enforced at design time).
- Entry arrays frozen after `BeginPlay` — stable flat index for all RPCs and net state.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| **One `UInteractionComponent` per actor** | Scanner uses `FindComponentByClass` — one result, no ambiguity. Multiple options are expressed as entries. |
| **Unified flat index (DataAsset + Inline)** | Stable `uint8` identifier for all RPCs and net state. Saves 1 byte per RPC. 255 cap is a design constraint, not a limitation. |
| **`FFastArraySerializer` for net state** | Only changed items replicate per cycle. Hundreds of interactables at steady state = near-zero bandwidth. |
| **Push Model replication** | `MarkItemDirty` only when state actually changes. No per-tick dirty scan across all interactable actors. |
| **`TArray<URequirement*>` on `FInteractionEntryConfig`** | Replaces `IInteractionConditionProvider`. Data-driven, designer-authored, no actor implementation required. AND is the common case; `URequirement_Composite` handles OR/NOT per slot. |
| **`URequirementList` wraps the entry array** | Each `FInteractionEntryConfig` owns a `URequirementList` reference (or null) instead of a raw array. This gives the Requirement System authority declaration (`ERequirementEvalAuthority`) and operator configuration. Entry requirements use `ClientValidated` authority — evaluated client-side for display, re-evaluated server-side for authority. |
| **`EInteractableState::Locked` is client-only** | Never replicated. Client assigns it when tag or requirement evaluation fails. Server sets only `Available`, `Occupied`, `Cooldown`, `Disabled`. |
| **`bServerEnabled` separate from `State`** | Administrative kill switch independent of gameplay state machine. Prevents accidental re-enable when game logic changes `State`. |
| **`GetInteractionLocation()` is BlueprintNativeEvent** | Overridable on both actor subclasses and Blueprint. Client and server call the same method — agreement is guaranteed as long as the override reads deterministic data. |
| **Hold RPC fires only on completion** | Server never knows about hold progress. Removes an entire class of timing attacks. |
| **Client pre-check before RPC** | Filters trivially-rejectable states client-side before sending a reliable RPC. Reduces server load under client-side prediction mismatches. |
| **`UInteractionDescriptorSubsystem` caches descriptors** | One `UObject` per descriptor class, regardless of how many actors reference it. Mandatory for MMO-scale actor counts. |
| **HISM not supported in this system** | HISM instances are not Actors. Extending this system for HISM would break the generic Actor abstraction. Correct approach: HISM Proxy Actor System (separate specification). |

---

## Logic Flow

### Scanning & Resolution (Owning Client Only)

```
Timer fires (ScanPeriod, default 0.2s)
  │
  ├─ IsDisabledByTag()?
  │    YES → ClearCurrentBest() → broadcast null → UI clears
  │
  └─ SphereOverlap(Interaction channel, ScanDistance)
       │
       ├─ Per hit: find UInteractionComponent via FindComponentByClass
       │           filter by MaxInteractionDistance
       │           score = Lerp(DistScore, AngleScore, ViewAngleWeight)
       │
       ├─ Select highest-scoring component as BestComp
       │
       └─ BestComp != CurrentBestComponent?
            YES → Unbind OnEntryStateChanged from previous
                  Bind  OnEntryStateChanged to new
                  If hold active on previous → CancelHold(TargetChanged)
                  Broadcast OnBestInteractableChanged
                  RunResolve()
            NO  → no-op (state changes handled by OnTrackedStateChanged)

RunResolve():
  CurrentBestComponent->ResolveOptions(Pawn, TargetActor, ResolveMode, ResolvedBuffer)
  Broadcast OnResolvedOptionsChanged → UI

ResolveOptions() per entry [i]:
  [a] !bServerEnabled OR State==Disabled → skip
  [b] Tag pre-filters (bitset AND, near-zero cost)
        SourceRequiredTags / SourceTagQuery on source
        TargetRequiredTags / TargetTagQuery on target
        Fail → State = Locked
  [c] EntryRequirements->Evaluate(Context) if not already Locked
        Fail → State = Locked, ConditionLabel from FailureReason
  [d] Append FResolvedInteractionOption to OutOptions
  [e] Exclusive + Available check → suppress all other options
  [f] Mode = Best: one winner per GroupTag (highest OptionPriority, non-Locked)
      Mode = All:  full sorted list including Locked
```

### Interaction Request (Owning Client → Server)

```
Player presses input
  │
  RequestInteract(EntryIndex)
    Client pre-check: bServerEnabled && State not Disabled/Occupied/Cooldown
    Resolved option not Locked
    │
    ├─ Press: ServerRequestInteract(ComponentRef, EntryIndex) [Reliable RPC]
    │         TriggerImmediateRescan()
    │
    └─ Hold: BeginHold(EntryIndex)
             Tick enabled
             HoldAccumulatedTime += DeltaTime each frame
             OnHoldProgressChanged [0..1] → UI progress bar
             Cancel: input release / player moved / disabling tag / target lost
             Complete: ServerRequestInteract(...) [Reliable RPC]
                       TriggerImmediateRescan()

ServerRequestInteract_Implementation:
  [1] Entry exists                          → reject EntryNotFound
  [2] bServerEnabled                        → reject EntryUnavailable
  [3] State not Occupied/Cooldown/Disabled  → reject EntryUnavailable
  [4] Distance + 75cm tolerance             → reject OutOfRange
  [5] Source tag checks                     → reject TagMismatch
  [6] Target tag checks                     → reject TagMismatch
  [7] EntryRequirements->Evaluate(Context)  → reject ConditionFailed
  ✅  ComponentRef->ExecuteEntry(EntryIndex, InstigatorPawn)
      OnInteractionConfirmed.Broadcast(ComponentRef, EntryIndex)
      ClientRPC_OnInteractionConfirmed(ComponentRef, EntryIndex)
  ❌  ClientRPC_OnInteractionRejected(EntryIndex, Reason)
```

### State Replication (Server → Clients)

```
Game system calls SetEntryState / SetEntryServerEnabled on server
  → FInteractionEntryNetState item marked dirty (MarkItemDirty)
  → FFastArraySerializer sends only that item on next replication cycle
  → PostReplicatedChange fires on clients
  → OnEntryStateChanged.Broadcast(Component, EntryIndex)
  → Scanner's OnTrackedStateChanged → RunResolve → OnResolvedOptionsChanged → UI
     (no scan — direct re-resolve on state change)
```

---

## Known Issues

| Issue | Severity | Notes |
|---|---|---|
| Scanner guard uses `IsLocallyControlled()` only | Low | Dynamic possession changes may leave scan timer running on wrong machine. Bind to `ReceivePossessed`/`ReceiveUnpossessed` if possession changes at runtime. |
| `CachedTagInterface` set once at `BeginPlay` | Low | Must call `RefreshCachedTagInterface()` manually if pawn's `ITaggedInterface` implementation changes at runtime. |
| `ScanDistance` must be ≥ max `MaxInteractionDistance` | Config | No runtime warning if misconfigured. Designer responsibility. |
| Dynamic primitive attachments not tracked by `UHighlightComponent` | Low | Primitives added after `BeginPlay` not included in `OwnedPrimitives`. Requires manual cache refresh. |
| `FResolvedInteractionOption::Label` is a raw `const FText*` | Low | Cannot be a UPROPERTY. Blueprint widgets must use a `BlueprintCallable` C++ wrapper to access it safely. |
| HISM instances not supported | By Design | Requires separate HISM Proxy Actor System. Do not extend this system for HISM. |

---

## File Structure

```
GameCore/
├── Source/
│   ├── GameCore/                              ← Runtime module
│   │   ├── GameCore.Build.cs                  ← Add: NetCore, GameplayTags, EnhancedInput
│   │   └── Interaction/
│   │       ├── Components/
│   │       │   ├── InteractionComponent.h/.cpp
│   │       │   ├── InteractionManagerComponent.h/.cpp
│   │       │   └── HighlightComponent.h/.cpp
│   │       ├── Data/
│   │       │   ├── InteractionEntryConfig.h         ← FInteractionEntryConfig struct
│   │       │   ├── InteractionEntryDataAsset.h/.cpp
│   │       │   └── InteractionNetState.h            ← FInteractionEntryNetState + Array
│   │       ├── Enums/
│   │       │   └── InteractionEnums.h               ← All enums (isolated for compile speed)
│   │       ├── ResolvedInteractionOption.h          ← FResolvedInteractionOption
│   │       └── UI/
│   │           ├── InteractionIconDataAsset.h/.cpp
│   │           ├── InteractionUIDescriptor.h/.cpp
│   │           └── InteractionDescriptorSubsystem.h/.cpp
│   │
│   └── GameCoreEditor/                        ← Editor-only module
│       ├── GameCoreEditor.Build.cs
│       └── Interaction/
│           └── InteractionComponentDetails.h/.cpp  ← Custom Details panel + validation
│
└── Content/
    └── (no plugin-default content — icons and post-process are project-provided)
```

### Build.cs Notes

`GameCore.Build.cs` public/private module names:
- `PublicDependencyModuleNames`: `Core`, `CoreUObject`, `Engine`, `GameplayTags`
- `PrivateDependencyModuleNames`: `NetCore`, `EnhancedInput`

`NetCore` is required for Push Model (`MARK_PROPERTY_DIRTY_FROM_NAME`, `GetMutableReplicationState`).
