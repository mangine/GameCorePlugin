# Quest System — Requirements and Design Decisions

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**Purpose:** Records the original system requirements, features added during design, key architectural decisions and their rationale, and features explicitly rejected.

---

## 1. Original System Requirements

| # | Requirement |
|---|---|
| R1 | Quests must have stages modelled as a state machine. The state machine code must be reused, not duplicated. |
| R2 | Quest definitions (stages, conditions, data) must be a single shared reference — never instantiated per player. |
| R3 | Each player must have replicated, per-quest runtime data: current stage and current progress data. |
| R4 | Progress tracking must handle two categories: counters that persist (e.g. kill counts) and conditions re-evaluated from live world state (e.g. inventory checks). |
| R5 | Quest building must be easy. Complex requirements and branching flows must be expressible via `URequirementList` and the state machine graph. |
| R6 | The quest system must emit GMS events for: quest started, completed, failed, stage started, stage completed. |
| R7 | Quest unlock detection must be reactive — watchers fire when conditions become met. |
| R8 | Client-side requirement checking must be supported for quest unlocks, with server validation as the authoritative gate. |
| R9 | Quest data must be persisted. Only active quests and the completed quest tag set are saved. Available quests are recalculated on login. |
| R10 | The quest system must not load hundreds of quest definitions per player on the server. Definitions are loaded on demand and ref-counted. |

---

## 2. Features Added During Design

### Quest Lifecycle Modes (`EQuestLifecycle`)
Four modes: `SingleAttempt`, `RetryUntilComplete`, `RetryAndAssist`, `Evergreen`. Covers every practical repeat pattern.

### Quest Cadence Resets (`EQuestResetCadence`)
Daily / Weekly / EventBound. Cooldown and cadence are expressed via `URequirement_QuestCooldown` in `UnlockRequirements` — not as fields on the definition. Gate logic belongs in the requirement system.

### `bEnabled` Live-Ops Kill Switch
Setting `bEnabled = false` removes the quest from active quests on login **without** adding `QuestCompletedTag`. Non-destructive — re-enabling the quest makes it immediately available again.

### Quest Display Data, Marker System, Categories, Reward Tables
All UI-facing and reward concerns are soft references or tag containers. The quest system never loads textures, icons, or loot tables at runtime.

### `UQuestConfigDataAsset`
Holds `MaxActiveQuests` and tunables. Referenced softly by `UQuestComponent`. Allows live-ops tuning without recompiling.

---

## 3. Shared Quest Extension

The base `UQuestComponent` and `UQuestDefinition` have zero group knowledge. The extension (`USharedQuestComponent`, `USharedQuestDefinition`) is opt-in.

- **`IGroupProvider`** — read-only interface for group data. Decouples from any concrete party system.
- **`UGroupProviderDelegates`** — delegate fallback for games that prefer loose wiring.
- **`OnRequestGroupEnrollment` delegate** — GMS round-trip rejected (no callback guarantee, party-owned tag dependency, no early-accept support). Replaced with a `TFunction` callback on the coordinator.
- **`EQuestMemberRole`** — lives in shared extension only. Base runtime stays lean.
- **No `MinGroupSize`/`MaxGroupSize` on `USharedQuestDefinition`** — group size is a requirement. It belongs in `UnlockRequirements` as `URequirement_GroupSize`.

---

## 4. Key Architectural Decisions

### `URequirementList` Used Everywhere, `URequirement` Never Held Directly
All quest requirement gates (`UnlockRequirements`, `CompletionRequirements`, transition rules) hold `TObjectPtr<URequirementList>`. This ensures the operator (AND/OR), authority declaration, and reactive registration API (`RegisterWatch`) are always available. Individual `URequirement` instances are never held directly by quest definitions.

### Reactive Unlock via `URequirementList::RegisterWatch`
Unlock detection uses `URequirementList::RegisterWatch(Owner, OnResult)`. The closure captures `QuestId`. When the watcher fires, the server notifies the client via `ClientRPC_NotifyQuestEvent(BecameAvailable/Unavailable)`. `URequirementList::RegisterWatch` delegates to `UGameCoreEventWatcher` (generic routing subsystem in the Event Bus system) — the quest system has no watcher subsystem of its own.

### Baseline Check at Login
After `RegisterUnlockWatchers`, each list is evaluated imperatively with `List->Evaluate(Ctx)` to establish the current availability state without waiting for an event. This covers the case where a player logs in already meeting requirements that were set while they were offline.

### Completion Evaluated Imperatively on Tracker Increment
When a tracker reaches `EffectiveTarget`, `Server_IncrementTracker` calls `EvaluateCompletionRequirementsNow` immediately. There is no reactive completion watcher. The watcher flush delay (even 100ms) is unacceptable for the completion animation and reward moment. `GameCoreEvent.Quest.TrackerUpdated` still broadcasts for external systems (achievements, analytics) that tolerate async delivery.

### Watcher Used Only for Unlock Detection
`URequirementList::RegisterWatch` is only registered for `UnlockRequirements`. Completion is a single imperative path. Routing completion through the watcher would add latency and require deduplication between the watcher and the imperative path.

### No Payload Injection into `FRequirementContext`
Previous versions injected tracker counters via `FRequirementContext::PersistedData` (`FRequirementPayload`). This was removed because:
- It made the requirement system carry tracker concerns it should not own.
- `FRequirementContext` v2 has no `PersistedData` field.
- Quest requirements that need tracker data call `PlayerState->FindComponentByClass<UQuestComponent>()` inside their own `Evaluate` override. The cost (pointer walk through a small component list) is acceptable for non-hot-path evaluation.

### No `UQuestComponent` Pointer in `FRequirementContext`
A previous version cached `TObjectPtr<UQuestComponent> QuestComponent` on `FRequirementContext` to avoid `FindComponentByClass` overhead. Removed because:
- It created a compile-time dependency from `Requirements/` (a zero-dependency module) on the Quest module.
- `FindComponentByClass<UQuestComponent>()` on `APlayerState` is called at most a few times per evaluation flush — not per frame. The overhead is negligible.

### `BuildRequirementContext` Returns a `FPlayerContext` Snapshot
`BuildRequirementContext()` wraps `APlayerState*` and `UWorld*` in a `FPlayerContext` struct, then returns `FRequirementContext::Make(CtxData)`. Quest requirements cast `Context.Data` to `FPlayerContext*` and retrieve whatever component data they need from `PlayerState`.

### Client-Side Completion Watchers for `ClientValidated` Quests
On the owning client, `RegisterClientValidatedCompletionWatchers` registers `URequirementList::RegisterWatch` on the `CompletionRequirements` of each active `ClientValidated` quest. When requirements pass client-side, the client fires `ServerRPC_RequestValidation`. The server re-evaluates from its authoritative context before advancing the stage. Client results are UI hints only — the server never acts on the client claim.

### Zero GMS Subscriptions in `UQuestComponent`
All tracker increments arrive via `Server_IncrementTracker` called by external bridge components. If the quest system subscribed to combat or inventory events, it would import those systems. The bridge component in the game module owns the wiring.

### State Machine Asset Reused as Stage Graph
`UQuestDefinition::StageGraph` is a `UStateMachineAsset`. Stage transitions use `UQuestTransitionRule : UTransitionRule` which evaluates a `URequirementList` against a `FRequirementContext*` passed as `ContextObject`. No new state machine code written.

### `UQuestRegistrySubsystem` as `UGameInstanceSubsystem`
Persists across world transitions. A `UWorldSubsystem` would destroy the definition cache and cadence clock on every loading screen.

### Available Quests Not Persisted
Only `ActiveQuests` and `CompletedQuestTags` are saved. Available quests are fully derivable from `CompletedQuestTags` + watcher re-evaluation at login. Persisting them would risk stale data diverging from quest definition changes.

### `bEnabled` Removal Is Non-Destructive
Removing a disabled active quest does NOT add `QuestCompletedTag`. Re-enabling the quest makes it immediately available again.

---

## 5. Explicitly Rejected Features

| Feature | Why Rejected |
|---|---|
| `EGroupRequirement` / `MinGroupSize` / `MaxGroupSize` on `USharedQuestDefinition` | Group size is a requirement. Belongs in `UnlockRequirements` as `URequirement_GroupSize`. |
| `bAllowPassiveGroupContribution` | No clear ownership. The game module bridge decides what triggers what. |
| GMS round-trip for `LeaderAccept` enrollment | No callback guarantee, no early-accept, coordinator depends on party-owned tag. Replaced by `TFunction` callback delegate. |
| `UStateMachineComponent` on `APlayerState` for quest stages | Fires unwanted side-effect events. `FGameplayTag` in `FQuestRuntime` + `FindFirstPassingTransition` is the lightweight alternative. |
| `NextStageTag` field on `UQuestStageDefinition` | Loses branching. State machine transition graph handles branching natively. |
| Storing `LastCompletedTimestamp` as `float` in payload | Precision loss past 2^24 seconds. `int64` must be read directly from `FQuestRuntime`. |
| `UWorldSubsystem` for `UQuestRegistrySubsystem` | Destroyed on world transitions, losing the definition cache and cadence clock. |
| Per-category active quest limits | Added complexity, unclear benefit. Single global `MaxActiveQuests` is sufficient. |
| Payload injection (`FRequirementPayload`) into `FRequirementContext` | Made requirement system carry tracker concerns it should not own. Requirement system v2 has no `PersistedData`. Quest requirements use `FindComponentByClass` instead. |
| `TObjectPtr<UQuestComponent>` cached on `FRequirementContext` | Created a dependency from `Requirements/` (zero-dependency module) on the Quest module. Removed. |
| `URequirementWatcherComponent` on `APlayerState` | Replaced by `URequirementList::RegisterWatch` which delegates to the generic `UGameCoreEventWatcher`. No per-player watcher component needed. |
| Reactive completion watcher | Adds flush delay to the completion moment. Imperatively evaluated in `Server_IncrementTracker` instead. |
