# Quest System

**Module:** `PirateGame.Quest` (game module, not GameCore plugin) 
**Status:** Specification — Pending Implementation 
**UE Version:** 5.7 
**Depends On:** GameCore (Requirement System, State Machine System, Serialization System, Event Bus)

---

## Sub-Pages

| File | Contents |
|---|---|
| [GameCore Changes](GameCore%20Changes.md) | `IGroupProvider`, `FRequirementPayload`, `FRequirementContext` addition, `URequirement_Persisted` |
| [Data Assets & Definitions](Data%20Assets%20and%20Definitions.md) | `UQuestDefinition`, `USharedQuestDefinition`, `UQuestStageDefinition`, all supporting structs and enums |
| [Runtime Structs](Runtime%20Structs.md) | `FQuestRuntime`, `FQuestTrackerEntry`, `FQuestRuntimeArray`, replication details |
| [UQuestComponent](UQuestComponent.md) | Per-player component: accept, fail, complete, persistence, watcher integration |
| [USharedQuestCoordinator](USharedQuestCoordinator.md) | Shared quest coordinator: group tracker, scaling, disconnect snapshot |
| [UQuestRegistrySubsystem](UQuestRegistrySubsystem.md) | World subsystem: definition loading, cadence resets, server clock |
| [Requirement Types](Requirement%20Types.md) | Quest-module-owned requirement subclasses |
| [GMS Events](GMS%20Events.md) | All GameplayMessage events emitted by the quest system |
| [File Structure](File%20Structure.md) | Module layout, Build.cs dependencies |

---

## Design Principles

- **Quest definitions are shared, immutable, server-loaded assets.** No per-player state lives in `UQuestDefinition`. The definition is the schema; `FQuestRuntime` is the instance.
- **The State Machine asset is the stage graph.** `UStateMachineAsset` drives stage transition logic. `UQuestComponent` reads the asset directly — `UStateMachineComponent` is NOT added to `APlayerState`. Stage progression is tracked via `FQuestRuntime::CurrentStageTag`.
- **Requirements evaluate against injected data.** Progress counters live in `FQuestTrackerEntry` and are injected into `FRequirementContext::PersistedData` via the watcher's `ContextBuilder` at evaluation time. Requirements are stateless.
- **`UQuestComponent` exposes a tracker increment API. It has zero GMS subscriptions.** External integration layers (game module) call `Server_IncrementTracker` when relevant events occur. The quest system never subscribes to combat, inventory, or any other system's events directly.
- **The watcher handles unlock detection. Completion is evaluated on tracker increment.** `URequirementWatcherComponent` watches `UnlockRequirements` reactively for all candidate quests. Stage `CompletionRequirements` are evaluated imperatively when `Server_IncrementTracker` is called and a tracker reaches its target — not through the watcher's flush cycle.
- **Authority is quest-level, not call-site.** `EQuestCheckAuthority` on `UQuestDefinition` determines whether unlock/completion validation runs server-first or client-first.
- **Available quests are not persisted.** Only `ActiveQuests` and `CompletedQuestTags` are saved. Available quests are recalculated on login by re-running watcher evaluation.
- **`bEnabled` is a live-ops kill switch.** Disabled quests are filtered from candidate lists. Active quests whose definition has `bEnabled = false` are removed from `ActiveQuests` on login without polluting `CompletedQuestTags`.
- **The quest system emits events; it never calls other systems.** Rewards, journal, achievements, and UI are downstream consumers of GMS events.
- **Shared quest support is a fully optional extension.** `USharedQuestComponent` inherits `UQuestComponent`. `USharedQuestDefinition` inherits `UQuestDefinition`. Dropping in the base types gives a complete solo quest system. The shared extension is opt-in per game.

---

## Authority Model

```
EQuestCheckAuthority::ServerAuthoritative
  Server evaluates UnlockRequirements / CompletionRequirements.
  On pass: server fires ClientRPC_NotifyQuestEvent to owning client.
  Use for: SingleAttempt quests, story gates, high-value rewards.

EQuestCheckAuthority::ClientValidated
  Client evaluates requirements using replicated FQuestRuntime data + injected payload.
  On pass: client fires ServerRPC_RequestValidation(QuestId, StageTag).
  Server re-evaluates from authoritative context (ContextBuilder called server-side).
  On server pass: server fires ClientRPC_NotifyQuestEvent.
  Use for: common side quests, daily quests, high-volume quests.
```

The `URequirementWatcherComponent` runs on:
- **Server always** — for `ServerAuthoritative` quests, drives unlock RPCs to client
- **Owning client** — for `ClientValidated` quests, drives local UI feedback and triggers server validation RPCs

---

## Tracker Increment — External Integration Pattern

`UQuestComponent::Server_IncrementTracker` is a public server-side API. It is called by whatever system in the game module owns the relevant event. The quest system has no knowledge of what triggered the increment.

```
Game module (e.g. UQuestTrackerBridge component on PlayerState):
  Subscribes to GameCoreEvent.Combat.MobKilled
  On event: resolves QuestId and TrackerKey from the killed mob's data
  Calls UQuestComponent::Server_IncrementTracker(QuestId, TrackerKey, 1)

UQuestComponent:
  Increments FQuestTrackerEntry::CurrentValue
  Clamps to EffectiveTarget
  Fires RequirementWatcherManager::NotifyPlayerEvent(RequirementEvent.Quest.TrackerUpdated)
  If tracker at target: evaluates CompletionRequirements immediately
  Fires GameCoreEvent.Quest.TrackerUpdated
```

This wiring bridge lives entirely in the game module — not in the quest system and not in the combat system.

---

## Quest Lifecycle State Machine (conceptual)

```
[Locked] ──(UnlockRequirements pass)──> [Available]
[Available] ──(Player accepts)──> [Active]
[Active] ──(All stages complete)──> [Completed]
[Active] ──(Fail condition met)──> [Failed]
[Failed / Completed] ──(Lifecycle rules)──> [Locked | Available | Active]
```

Lifecycle rules per `EQuestLifecycle`:

| Lifecycle | On Fail | On Complete |
|---|---|---|
| `SingleAttempt` | Permanently closed, `CompletedQuestTags` receives fail tag | Permanently closed |
| `RetryUntilComplete` | Reset to Available (cooldown if set) | Permanently closed |
| `RetryAndAssist` | Reset to Available (cooldown if set) | Player can re-enter as Helper |
| `Evergreen` | Reset to Available (cooldown if set) | Reset to Available (cooldown if set) |
