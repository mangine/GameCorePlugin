# Quest System

**Module:** `PirateGame.Quest` (game module, not GameCore plugin) 
**Status:** Specification — Pending Implementation 
**UE Version:** 5.7 
**Depends On:** GameCore (Requirement System, State Machine System, Serialization System, Event Bus)

---

## Sub-Pages

| File | Contents |
|---|---|
| [GameCore Changes](GameCore%20Changes.md) | Additions to `FRequirementContext`, `FRequirementPayload`, `URequirement_Persisted` |
| [Data Assets & Definitions](Data%20Assets%20and%20Definitions.md) | `UQuestDefinition`, `UQuestStageDefinition`, all supporting structs and enums |
| [Runtime Structs](Runtime%20Structs.md) | `FQuestRuntime`, `FQuestTrackerEntry`, `FQuestMemberState`, replication details |
| [UQuestComponent](UQuestComponent.md) | Per-player component: accept, fail, complete, persistence, watcher integration |
| [UQuestRegistrySubsystem](UQuestRegistrySubsystem.md) | World subsystem: definition loading, cadence resets, server clock |
| [UPartyQuestCoordinator](UPartyQuestCoordinator.md) | Party component: shared tracker, scaling, disconnect snapshot |
| [Requirement Types](Requirement%20Types.md) | All quest-module requirement subclasses |
| [GMS Events](GMS%20Events.md) | All GameplayMessage events emitted by the quest system |
| [File Structure](File%20Structure.md) | Module layout, Build.cs dependencies |

---

## Design Principles

- **Quest definitions are shared, immutable, server-loaded assets.** No per-player state lives in `UQuestDefinition`. The definition is the schema; `FQuestRuntime` is the instance.
- **The State Machine is the stage graph.** `UStateMachineAsset` drives stage transitions. No new state machine code is written. Stage nodes and transition rules are subclassed from `UStateNodeBase` and `UTransitionRule`.
- **Requirements evaluate against injected data.** Progress counters are not stored on requirements. They live in `FQuestTrackerEntry` and are injected into `FRequirementContext::PersistedData` at evaluation time.
- **Authority is quest-level, not call-site.** `EQuestCheckAuthority` on `UQuestDefinition` determines whether unlock/completion validation runs server-first or client-first.
- **Available quests are not persisted.** Only `ActiveQuests` and `CompletedQuestTags` are saved. Available quests are recalculated on login by re-running watcher evaluation.
- **The quest system emits events; it does not call other systems.** Rewards, journal entries, achievements, and UI are downstream consumers of GMS events.
- **Party coordination is a separate component.** `UQuestComponent` handles solo and party-member flows. `UPartyQuestCoordinator` owns the shared tracker truth for party quests.

---

## Authority Model

```
EQuestCheckAuthority::ServerAuthoritative
  Server evaluates UnlockRequirements / CompletionRequirements.
  On pass: server fires ClientRPC_NotifyQuestEvent to owning client.
  Use for: OneTry quests, story gates, high-value rewards.

EQuestCheckAuthority::ClientValidated
  Client evaluates requirements using replicated FQuestRuntime data.
  On pass: client fires ServerRPC_RequestValidation(QuestId, StageTag).
  Server re-evaluates from authoritative context. Never trusts client result.
  On server pass: server fires ClientRPC_NotifyQuestEvent.
  Use for: common side quests, daily quests, high-volume quests.
```

The `URequirementWatcherComponent` runs on:
- **Server always** — for `ServerAuthoritative` quests, drives unlock RPCs to client
- **Owning client** — for `ClientValidated` quests, drives local UI feedback and triggers server validation RPCs

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
