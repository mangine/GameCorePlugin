# Quest System

**Module:** `GameCore` (plugin) 
**Status:** Specification — Pending Implementation 
**UE Version:** 5.7 
**Depends On:** GameCore (Requirement System, State Machine System, Serialization System, Event Bus)

---

## Sub-Pages

| File | Contents |
|---|---|
| [Requirements and Design Decisions](Requirements%20and%20Design%20Decisions.md) | Original requirements, features added during design, architectural decisions and rationale, rejected features |
| [GameCore Changes](GameCore%20Changes.md) | Cross-reference: GameCore additions made for the quest system |
| [Data Assets & Definitions](Data%20Assets%20and%20Definitions.md) | `UQuestDefinition`, `USharedQuestDefinition`, `UQuestStageDefinition`, all supporting structs and enums |
| [Runtime Structs](Runtime%20Structs.md) | `FQuestRuntime`, `FQuestTrackerEntry`, `FQuestRuntimeArray`, replication details |
| [UQuestComponent](UQuestComponent.md) | Per-player component: accept, fail, complete, persistence, watcher integration |
| [USharedQuestCoordinator](USharedQuestCoordinator.md) | Shared quest coordinator: group tracker, scaling, disconnect snapshot |
| [UQuestRegistrySubsystem](UQuestRegistrySubsystem.md) | Game instance subsystem: definition loading, cadence resets, server clock |
| [Requirement Types](Requirement%20Types.md) | Quest-module-owned requirement subclasses |
| [GMS Events](GMS%20Events.md) | All GameplayMessage events emitted by the quest system |
| [Integration Guide](Integration%20Guide.md) | Setup checklist, tracker bridge pattern, UI integration, system interfaces |
| [File Structure](File%20Structure.md) | Module layout, Build.cs dependencies |

---

## Design Principles

- **Quest definitions are shared, immutable, server-loaded assets.** No per-player state lives in `UQuestDefinition`. The definition is the schema; `FQuestRuntime` is the instance.
- **The State Machine asset is the stage graph.** `UStateMachineAsset` drives stage transition logic. `UQuestComponent` reads the asset directly via `UStateMachineAsset::FindFirstPassingTransition` — `UStateMachineComponent` is NOT added to `APlayerState`. Stage progression is tracked via `FQuestRuntime::CurrentStageTag`.
- **Stage transitions use `UQuestTransitionRule`.** A `UTransitionRule` subclass that evaluates a `URequirementList` against the quest’s `FRequirementContext`. Designers author branching stage graphs using the same requirement system as everywhere else.
- **Requirements evaluate against injected data.** Progress counters live in `FQuestTrackerEntry` and are injected into `FRequirementContext::PersistedData` via the watcher’s `ContextBuilder` at evaluation time. Requirements are stateless.
- **`UQuestComponent` exposes a tracker increment API. It has zero GMS subscriptions.** External integration layers call `Server_IncrementTracker` when relevant events occur. The quest system never subscribes to combat, inventory, or any other system’s events.
- **Completion is evaluated imperatively on tracker increment.** When a tracker reaches its `EffectiveTarget`, `UQuestComponent` immediately evaluates `CompletionRequirements` and resolves the stage. The watcher flush is not involved in completion evaluation.
- **Unlock requirements use the watcher reactively.** `URequirementWatcherComponent` watches `UnlockRequirements` for all candidate quests. Authority is per-quest (`EQuestCheckAuthority`): `ServerAuthoritative` runs watcher server-only; `ClientValidated` runs watcher on both sides with server re-validation.
- **Authority is quest-level, not call-site.** `EQuestCheckAuthority` on `UQuestDefinition` governs both unlock and completion validation paths.
- **Available quests are not persisted.** Only `ActiveQuests` and `CompletedQuestTags` are saved. Available quests are recalculated on login by re-running watcher evaluation.
- **`bEnabled` is a live-ops kill switch.** Disabled quests are filtered from candidate lists. Active disabled quests are removed on login without polluting `CompletedQuestTags`.
- **The quest system emits events; it never calls other systems.** Rewards, journal, achievements, and UI are downstream consumers of GMS events.
- **Shared quest support is a fully optional extension.** Dropping in the base types gives a complete solo quest system. The shared extension is opt-in.

---

## Authority Model

```
EQuestCheckAuthority::ServerAuthoritative
  Unlock watcher: runs SERVER only.
    On pass → server fires ClientRPC_NotifyQuestEvent(BecameAvailable).
  Completion: server evaluates, notifies client via ClientRPC.
  Use for: SingleAttempt quests, story gates, high-value rewards.

EQuestCheckAuthority::ClientValidated
  Unlock watcher: runs on SERVER and OWNING CLIENT.
    Client watcher fires ServerRPC_AcceptQuest on pass (UI responsiveness).
    Server always re-evaluates before accepting.
  Completion: client watcher fires ServerRPC_RequestValidation on pass.
    Server re-evaluates authoritatively. Never trusts client result.
  Use for: common side quests, daily quests, high-volume quests.
```

The `URequirementWatcherComponent` runs on:
- **Server always** — evaluates all registered unlock sets authoritatively
- **Owning client** — for `ClientValidated` quests only, drives UI feedback and triggers server RPCs

---

## Tracker Increment — External Integration Pattern

`UQuestComponent::Server_IncrementTracker` is a public server-side API. It is called by whatever system in the game module owns the relevant event. The quest system has no knowledge of what triggered the increment.

```
Game module (e.g. UQuestTrackerBridge on APlayerState):
  Subscribes to GameCoreEvent.Combat.MobKilled
  Resolves QuestId and TrackerKey from the killed mob’s data
  Calls UQuestComponent::Server_IncrementTracker(QuestId, TrackerKey, 1)

UQuestComponent:
  Increments FQuestTrackerEntry::CurrentValue
  Clamps to EffectiveTarget
  Fires RequirementWatcherManager::NotifyPlayerEvent(RequirementEvent.Quest.TrackerUpdated)
  If tracker at target: evaluates CompletionRequirements immediately
  Fires GameCoreEvent.Quest.TrackerUpdated
```

This wiring bridge lives entirely in the game module.

---

## Quest Lifecycle

```
[Locked] ──(UnlockRequirements pass)──> [Available]
[Available] ──(Player accepts)──> [Active]
[Active] ──(All stages complete)──> [Completed]
[Active] ──(Fail condition met)──> [Failed]
[Failed / Completed] ──(Lifecycle rules)──> [Locked | Available | Active]
```

| Lifecycle | On Fail | On Complete |
|---|---|---|
| `SingleAttempt` | Permanently closed, `QuestCompletedTag` added | Permanently closed |
| `RetryUntilComplete` | Reset to Available (cooldown if set) | Permanently closed |
| `RetryAndAssist` | Reset to Available (cooldown if set) | Player can re-enter as Helper |
| `Evergreen` | Reset to Available (cooldown if set) | Reset to Available (cooldown if set) |
