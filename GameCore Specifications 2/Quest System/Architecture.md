# Quest System — Architecture

**Module:** Game module (not GameCore plugin)
**Status:** Specification — Pending Implementation
**UE Version:** 5.7

---

## Overview

The Quest System provides per-player quest tracking, stage progression, unlock detection, and persistence for an MMORPG. It is a self-contained module with zero subscriptions to game events — all progress enters via a public server-side API. The quest system emits events downstream; it never calls other systems directly.

The system is split into a **base solo layer** and an **optional shared quest extension**. Dropping in the base types gives a complete single-player quest experience. The shared extension (`USharedQuestComponent` + `USharedQuestCoordinator`) is opt-in and adds no overhead when absent.

---

## Dependencies

### GameCore Plugin Systems

| System | Usage |
|---|---|
| **Requirement System** | `URequirementList` for unlock, completion, and transition rules. `URequirementList::RegisterWatch` for reactive unlock detection. |
| **State Machine System** | `UStateMachineAsset` as the stage graph. `UQuestTransitionRule` + `UQuestStateNode` extensions. `FindFirstPassingTransition` evaluated directly by `UQuestComponent` — no `UStateMachineComponent` on `APlayerState`. |
| **Serialization System** | `IPersistableComponent` implemented by `UQuestComponent`. `NotifyDirty` triggers saves. |
| **Event Bus System** | `UGameCoreEventBus` for all outbound GMS broadcasts. Zero inbound subscriptions. |
| **GameCore Core** | `IGroupProvider` interface for shared quest group data reads. |

### Unreal Engine Modules

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine",
    "GameplayTags",
    "GameCore",   // Requirements, StateMachine, Serialization, EventBus, IGroupProvider
    "NetCore",    // FFastArraySerializer
});
```

### Runtime Dependencies (not compile-time)

- `UAssetManager` — definition loading and `PrimaryAssetId` enumeration.
- `APlayerState` — `UQuestComponent` lives here.
- External group/party actor — hosts `USharedQuestCoordinator` (shared extension only).

---

## Requirements

| # | Requirement |
|---|---|
| R1 | Quest stages are modelled as a state machine. `UStateMachineAsset` is reused — no new state machine code. |
| R2 | Quest definitions are shared, immutable, server-loaded assets. No per-player state in definitions. |
| R3 | Per-player runtime data (`FQuestRuntime`) is replicated to the owning client via `FFastArraySerializer`. |
| R4 | Progress tracking handles persistent counters (`FQuestTrackerEntry`) and re-evaluated conditions (`bReEvaluateOnly`). |
| R5 | All requirement gates use `URequirementList`. Branching is expressed via the state machine transition graph. |
| R6 | GMS events emitted for: started, completed, failed, abandoned, stage started/completed/failed, tracker updated, daily/weekly reset. |
| R7 | Unlock detection is reactive via `URequirementList::RegisterWatch`. |
| R8 | `ClientValidated` quests run watcher on client for UI responsiveness; server always re-validates. |
| R9 | Active quests and `CompletedQuestTags` are persisted. Available quests are recalculated on login. |
| R10 | Definitions are loaded on-demand and ref-counted. Never loaded per-player upfront. |

---

## Features

- **Four lifecycle modes:** `SingleAttempt`, `RetryUntilComplete`, `RetryAndAssist`, `Evergreen`.
- **Cadence resets:** Daily/Weekly/EventBound. Expressed as `URequirement_QuestCooldown` in `UnlockRequirements` — not hardcoded fields.
- **`bEnabled` kill switch:** Non-destructive. Disabled active quests are removed on login without adding `QuestCompletedTag`.
- **Group scaling:** `FQuestProgressTrackerDef::ScalingMultiplier` scales tracker targets by group size. `GroupSize == 1` always returns `TargetValue`.
- **Display metadata:** Soft `UTexture2D`, localised `FText`, difficulty enum — never loaded by the quest system.
- **`UQuestConfigDataAsset`:** Externalizes `MaxActiveQuests` and other tunables. No recompile required for tuning.
- **Shared quest extension:** `USharedQuestCoordinator` owns the shared tracker truth. `IGroupProvider` decouples from any concrete party type. `OnRequestGroupEnrollment` delegate integrates `LeaderAccept` without GMS round-trips.

---

## Design Decisions

### `FQuestEvaluationContext` as `FInstancedStruct` payload

The v2 `FRequirementContext` uses `FInstancedStruct Data` — no typed fields. Quest requirements receive a `FQuestEvaluationContext` snapshot struct injected into `Data`. This struct carries `APlayerState*` and `UWorld*`. Quest-owned requirements that need component data (`URequirement_QuestCompleted`, `URequirement_QuestCooldown`) retrieve `UQuestComponent` via `PlayerState->FindComponentByClass<UQuestComponent>()` inside their own `Evaluate` override.

**Why not cache `UQuestComponent*` in the context:** would create a compile-time import from the zero-dependency `Requirements/` module into the Quest module. `FindComponentByClass` on `APlayerState` is called at most a few times per evaluation flush — not per frame. Overhead is negligible.

### State Machine Asset as Stage Graph

`UQuestDefinition::StageGraph` is a `UStateMachineAsset`. `UStateMachineComponent` is **not** added to `APlayerState` — it would fire unwanted side-effect events. Instead, `UQuestComponent` calls `StageGraph->FindFirstPassingTransition(CurrentStageTag, &Ctx)` directly. Stage state is `FQuestRuntime::CurrentStageTag`.

`UQuestStateNode` extends `UStateNodeBase` with `bIsCompletionState` / `bIsFailureState`. `UQuestTransitionRule` extends `UTransitionRule` to evaluate a `URequirementList` against a `FRequirementContext*` ContextObject.

### Completion Evaluated Imperatively, Not Reactively

When a tracker hits `EffectiveTarget`, `Server_IncrementTracker` immediately calls `EvaluateCompletionRequirementsNow`. There is no reactive completion watcher. Even a 100ms flush delay is unacceptable for the completion moment. The watcher is used **exclusively for unlock detection**.

### Zero GMS Subscriptions

`UQuestComponent` has no GMS listeners. All tracker increments arrive via `Server_IncrementTracker` called by an external bridge component in the game module. This prevents any import of combat, inventory, or party systems into the quest module.

### Available Quests Not Persisted

Only `ActiveQuests` and `CompletedQuestTags` are saved. Available quests are fully derivable from `CompletedQuestTags` + watcher re-evaluation at login. Persisting them risks stale divergence after content updates.

### `UGameInstanceSubsystem` for Registry

`UQuestRegistrySubsystem` persists across world transitions. A `UWorldSubsystem` would destroy the definition cache and cadence clock on every loading screen.

### `OnRequestGroupEnrollment` Delegate, Not GMS

GMS round-trip for `LeaderAccept` enrollment was rejected: no callback guarantee, party-owned tag dependency, no early-accept. Replaced with a `TDelegate` callback — the group system owns the grace timer and calls back with the confirmed member list.

---

## Logic Flow

### Quest Lifecycle

```
[Locked] ──(UnlockRequirements pass)──> [Available]
[Available] ──(Player accepts / ServerRPC_AcceptQuest)──> [Active]
[Active] ──(All stages complete)──> [Completed]
[Active] ──(Fail condition met)──> [Failed]
[Failed / Completed] ──(Lifecycle rules)──> [Locked | Available | Active]
```

### Unlock Detection (Server)

```
BeginPlay (server)
  ValidateActiveQuestsOnLogin()
    └─ Async load each active quest definition
        └─ Remove if bEnabled == false (non-destructive)
        └─ When all loads complete:
             RegisterUnlockWatchers()
               ├─ Iterate all known quest asset IDs from registry
               ├─ ShouldWatchUnlock() gate (not active, not closed, bEnabled)
               ├─ Def->UnlockRequirements->RegisterWatch(this, closure)
               └─ Immediate baseline Evaluate() → ClientRPC_NotifyQuestEvent
```

### Accept Flow

```
ServerRPC_AcceptQuest(QuestId)
  1. Capacity / duplicate / lifecycle pre-filter
  2. GetOrLoadDefinitionAsync → bEnabled + UnlockRequirements.Evaluate (server)
  3. Create FQuestRuntime, Internal_InitTrackers
  4. ActiveQuests.Add, MarkArrayDirty, AddReference, NotifyDirty
  5. Unregister unlock watcher handle
  6. Broadcast Quest.Started, Quest.StageStarted
  7. ClientRPC_NotifyQuestEvent(Started)
```

### Tracker Increment Flow

```
Server_IncrementTracker(QuestId, TrackerKey, Delta)
  1. Find FQuestRuntime* + FQuestTrackerEntry*
  2. Clamp CurrentValue += Delta to EffectiveTarget
  3. MarkItemDirty, NotifyDirty
  4. Broadcast Quest.TrackerUpdated
  5. If CurrentValue >= EffectiveTarget:
       EvaluateCompletionRequirementsNow(QuestId)  ← imperative, immediate
```

### Completion / Stage Advance

```
EvaluateCompletionRequirementsNow(QuestId)
  ├─ Load definition from registry (must be resident)
  ├─ Find current UQuestStageDefinition
  ├─ Stage.CompletionRequirements.Evaluate(BuildRequirementContext())
  └─ On pass:
       if bIsCompletionState → Internal_CompleteQuest
       if bIsFailureState    → Internal_FailQuest
       else ResolveNextStage → Internal_AdvanceStage

Internal_AdvanceStage
  ├─ ResolveNextStage: StageGraph.FindFirstPassingTransition(CurrentStageTag, Ctx)
  ├─ Internal_InitTrackers for new stage
  ├─ MarkItemDirty, NotifyDirty
  └─ Broadcast Quest.StageCompleted + Quest.StageStarted
```

### ClientValidated Completion Path

```
Client RegisterClientValidatedCompletionWatchers (owning client)
  └─ Stage.CompletionRequirements.RegisterWatch → fires ServerRPC_RequestValidation

ServerRPC_RequestValidation(QuestId, StageTag)
  1. Runtime.CurrentStageTag == StageTag guard
  2. GetOrLoadDefinitionAsync → bEnabled check
  3. Server-side Evaluate(BuildRequirementContext())
  4. Pass → advance/complete. Fail → ClientRPC_NotifyValidationRejected
```

### Login Sequence (Server)

```
BeginPlay (server)
  1. Load UQuestConfigDataAsset synchronously
  2. ValidateActiveQuestsOnLogin (async, fan-out)
     a. Load each active quest definition
     b. Remove disabled quests (non-destructive)
     c. When all resolved → RegisterUnlockWatchers
        i.  RegisterWatch for all candidates (reactive)
        ii. Immediate Evaluate → ClientRPC baseline

BeginPlay (owning client)
  1. RegisterClientValidatedCompletionWatchers
```

---

## Authority Model

```
EQuestCheckAuthority::ServerAuthoritative
  Unlock watcher: server only.
    Pass → ClientRPC_NotifyQuestEvent(BecameAvailable)
  Completion: server evaluates, notifies client via ClientRPC.
  Use for: story gates, SingleAttempt, high-value rewards.

EQuestCheckAuthority::ClientValidated
  Unlock watcher: server + owning client.
    Client pass → ServerRPC_AcceptQuest (UI responsiveness)
    Server always re-evaluates.
  Completion: client watcher → ServerRPC_RequestValidation
    Server re-evaluates authoritatively. Never trusts client result.
  Use for: common side quests, daily quests, high-volume quests.
```

> The server **always** re-evaluates requirements before any gameplay action regardless of authority mode. Client results are UI hints only.

---

## Known Issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| KI-1 | `IterateAllDefinitions` on `UQuestRegistrySubsystem` is not yet fully specified — the sync iteration only covers resident definitions; quests whose assets are not yet loaded are silently skipped during watcher registration at login. For large quest counts this means newly-eligible quests may not fire `BecameAvailable` until the next event. | Medium | Mitigated by the immediate baseline check. Full fix requires pre-enumerating all asset IDs at initialize time. |
| KI-2 | `USharedQuestComponent::Server_IncrementTracker` routes through the coordinator but the spec does not define behaviour when the coordinator has been GC'd while the member is still active (e.g. group actor destroyed mid-quest). | Medium | Coordinator should hold a weak pointer guard; component should fall back to base solo increment. |
| KI-3 | Client-side completion watchers registered in `RegisterClientValidatedCompletionWatchers` are not re-registered when `ActiveQuests` replicates a new quest after login (i.e. a quest accepted after `BeginPlay`). | Medium | `OnRep_ActiveQuests` (via `PostReplicatedAdd`) should trigger registration for newly added quests. |
| KI-4 | The de-scale formula in `USharedQuestCoordinator::BuildDeScaledSnapshot` uses integer `Floor` which can over-reward a leaving member in edge cases with very high `ScalingMultiplier` values. | Low | Cap at `SoloTarget` is applied but the intermediate floor may still produce values slightly above solo intent. |
| KI-5 | `UQuestRegistrySubsystem::ResolveQuestPath` uses a string-based leaf-name match which is O(n) over `AllQuestAssetIds`. For games with 1000+ quests this is mildly expensive at first load. | Low | Can be replaced with a pre-built `TMap<FName, FPrimaryAssetId>` at `Initialize` time. |

---

## File Structure

```
Source/PirateGame/Quest/
├── Quest.Build.cs
├── Enums/
│   └── QuestEnums.h
├── Data/
│   ├── QuestDefinition.h / .cpp
│   ├── SharedQuestDefinition.h / .cpp
│   ├── QuestStageDefinition.h / .cpp
│   ├── QuestDisplayData.h
│   ├── QuestProgressTrackerDef.h
│   ├── QuestConfigDataAsset.h
│   └── QuestMarkerDataAsset.h / .cpp
├── Runtime/
│   └── QuestRuntime.h / .cpp
├── Components/
│   ├── QuestComponent.h / .cpp
│   ├── SharedQuestComponent.h / .cpp
│   └── SharedQuestCoordinator.h / .cpp
├── Subsystems/
│   └── QuestRegistrySubsystem.h / .cpp
├── Requirements/
│   ├── Requirement_QuestCompleted.h / .cpp
│   ├── Requirement_QuestCooldown.h / .cpp
│   └── Requirement_ActiveQuestCount.h
└── Events/
    └── QuestEventPayloads.h

GameCore Plugin Additions (no quest-specific code):
Source/GameCore/
├── Interfaces/
│   └── GroupProvider.h                   ← IGroupProvider, UGroupProviderDelegates
└── StateMachine/
    ├── QuestStateNode.h / .cpp           ← UQuestStateNode : UStateNodeBase
    └── QuestTransitionRule.h / .cpp      ← UQuestTransitionRule : UTransitionRule
```

### Asset Manager Configuration

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="QuestDefinition",
    AssetBaseClass=/Script/GameCore.QuestDefinition,
    bHasBlueprintClasses=False,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Quests"))
)
```

**Convention:** Asset file name must match the leaf node of `QuestDefinition::QuestId`. e.g. `Quest.Id.TreasureHunt` → asset named `TreasureHunt`. Validated by `UQuestDefinition::IsDataValid`.
