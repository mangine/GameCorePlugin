# Quest System — Requirements and Design Decisions

**Sub-page of:** [Quest System](Quest%20System%20Overview.md) 
**Purpose:** Records the original system requirements, features added during design, key architectural decisions and their rationale, and features explicitly rejected. This file is the authoritative record of *why* the system is designed the way it is.

---

## 1. Original System Requirements

These were the founding requirements as stated at the start of design. Every major system component traces back to at least one of these.

| # | Requirement |
|---|---|
| R1 | Quests must have stages modelled as a state machine. The state machine code must be reused, not duplicated. |
| R2 | Quest definitions (stages, conditions, data) must be a single shared reference — never instantiated per player. |
| R3 | Each player must have replicated, per-quest runtime data: current stage and current progress data. |
| R4 | Progress tracking must handle two categories: counters that must persist (e.g. kill counts) and conditions that can be re-evaluated from live world state (e.g. inventory checks). |
| R5 | Quest building must be easy. Complex requirements and branching flows must be expressible via the existing URequirement system and the state machine graph. |
| R6 | The quest system must emit GMS events for: quest started, completed, failed, stage started, stage completed. |
| R7 | The system must use `URequirementWatcherComponent` to unlock quests reactively when conditions are met. |
| R8 | Client-side requirement checking must be supported for quest unlocks, with server validation as the authoritative gate. |
| R9 | Quest data must be persisted. Only active quests and the completed quest tag set are saved. Available quests are recalculated on login. |
| R10 | The quest system must not load hundreds of quest definitions per player on the server. Definitions are loaded on demand and ref-counted. |

---

## 2. Features Added During Design

These features were not in the original requirements but were added during the design process to meet AAA quality standards, usability needs, or to solve specific problems that emerged.

### Quest Lifecycle Modes (`EQuestLifecycle`)
Four modes covering every practical quest repeat pattern:
- `SingleAttempt` — fail or complete: permanently closed. No retry, no party help.
- `RetryUntilComplete` — fail resets to available. Cooldown applies if set.
- `RetryAndAssist` — as above, plus player can rejoin a group as Helper after completing.
- `Evergreen` — always repeatable. Resets to available after each completion or failure.

### Quest Cadence Resets (`EQuestResetCadence`)
Daily (00:00 UTC) and Weekly (Monday 00:00 UTC) resets hardcoded to UTC. `EventBound` cadence governed by `ExpiryTimestamp`. Cooldown and cadence are expressed via `URequirement_QuestCooldown` in `UnlockRequirements`, not as fields on the definition. This keeps gate logic in the requirement system where it belongs.

### `bEnabled` Live-Ops Kill Switch
Every `UQuestDefinition` has `bEnabled = true`. Setting it `false` excludes the quest from candidate unlock lists and removes it from active quests on login **without** adding `QuestCompletedTag`. This means re-enabling the quest makes it immediately available again. Non-destructive by design — safe to use as a hotfix for bugged quests.

### Quest Display Data (`FQuestDisplayData`)
All UI-facing fields grouped in one struct: `FText` Title, ShortDescription, LongDescription (all localizable), `EQuestDifficulty` enum, soft `UTexture2D` reference. Nothing in `FQuestDisplayData` is loaded at runtime by the quest system — the UI owns loading.

### Quest Marker System
`QuestMarkerTag` (`FGameplayTag`) on `UQuestDefinition` maps to an icon via `UQuestMarkerDataAsset` (tag → soft texture). Fully designer-extensible — new marker types require no C++ changes. Replaces the `EQuestMarkerType` enum that was initially proposed; tags give unlimited extensibility.

### Quest Categories
`QuestCategories` (`FGameplayTagContainer`) for free-form UI filtering and grouping. Multiple categories per quest allowed (e.g. `Quest.Category.Story` + `Quest.Category.Combat`). No enum; no central registry.

### Reward Tables
`FirstTimeRewardTable` and `RepeatingRewardTable` as soft `ULootTable` references on `UQuestDefinition`. The quest system emits these as soft references in the `FQuestCompletedPayload` GMS event. The reward system loads and grants. Quest system never touches loot tables at runtime.

### `UQuestConfigDataAsset`
A plain `UDataAsset` referenced softly by `UQuestComponent`. Holds `MaxActiveQuests` and any future system tunables. Allows changing quest log capacity without recompiling. Initially specced as a hardcoded UPROPERTY on the component; moved to a data asset so designers and live-ops can tune it without code changes.

### Active Quest Limit
Global cap enforced server-side at `ServerRPC_AcceptQuest`. Client reads `ActiveQuests.Items.Num()` for pre-validation UI hints. Per-category limits were considered but deferred — current design uses a single global cap for simplicity.

### Quest Expiry
`ExpiryTimestamp` (`int64` Unix timestamp, 0 = no expiry) on `UQuestDefinition`. Primarily for dungeon and event quests where unlimited active time is a design problem. A quest expiring is treated as a failure and follows `EQuestLifecycle` rules.

---

## 3. Shared Quest Extension

The shared quest system is a fully optional extension. The base `UQuestComponent` and `UQuestDefinition` have zero group knowledge. This section records why the extension is designed the way it is.

### Why It Is a Separate Extension
The base quest system must work for singleplayer games, online games without parties, and online games with parties. Baking group logic into the base would force every user of the quest system to carry group concepts they don't need. The extension is opt-in: replace `UQuestComponent` with `USharedQuestComponent`, and replace `UQuestDefinition` with `USharedQuestDefinition`.

### `IGroupProvider` — Interface over Concrete Dependency
The shared quest extension needs group data (group size, leader status, member list, group actor). Importing a concrete party system would couple the quest system to one specific grouping implementation. `IGroupProvider` is a four-method read-only interface in GameCore. Any grouping system implements it. The quest extension never imports a party module.

### `UGroupProviderDelegates` — Delegate Fallback
Implementing `IGroupProvider` requires subclassing `APlayerState`. For games that want loose wiring or are still building their party system, `UGroupProviderDelegates` provides a component-based delegate binding pattern. Add the component, implement `IGroupProvider` forwarding on `APlayerState`, bind the delegates later. Discussed and chosen over making delegates the primary mechanism because three related methods with enforced completeness (interface) is safer than three independent delegates that can be partially bound.

### `GetGroupActor()` on `IGroupProvider`
Added to allow `USharedQuestComponent` to locate the `USharedQuestCoordinator` without knowing the concrete type of the group actor. The component calls `GetGroupActor()` then `FindComponentByClass<USharedQuestCoordinator>()`. A coordinator registry subsystem was considered but rejected because leadership transfers would create complex state management problems in a registry.

### `OnRequestGroupEnrollment` Delegate
The `LeaderAccept` flow requires: notifying group members, managing an opt-out grace window, resolving the confirmed member list, and optionally resolving early if all members accept before the timer expires. Initially designed using GMS events (`GroupInvite` → `QuestEnrollmentReady` round-trip). **Rejected** because:
- The coordinator would depend on a party-system event tag it cannot own.
- If the party system never fires `QuestEnrollmentReady`, the coordinator has a dangling state with no cleanup.
- Early accept (all members confirm before timer) is awkward — the party system must know to fire the event early.

Replaced with a single `TDelegate` on `USharedQuestCoordinator` that takes the quest ID, invited members, grace seconds, and a `TFunction` callback. The party system binds it, manages the timer and opt-outs internally, and calls the callback when ready (early or on timeout). The callback is a contractual guarantee — it must always fire. No GMS round-trip, no dangling state.

### Shared Trackers and Scaling
`USharedQuestCoordinator` owns the authoritative shared counter for each tracker. `ScalingMultiplier` on `FQuestProgressTrackerDef` scales tracker targets with group size. When a member leaves, their snapshot is de-scaled: `Min(CurrentShared, Floor(CurrentShared / ScalingMultiplier))` capped at `SoloTarget`. This prevents players from abusing scaling by joining a group, running trackers up, then leaving with inflated progress.

### No Group Size Check on `USharedQuestDefinition`
Early design had `EGroupRequirement`, `MinGroupSize`, `MaxGroupSize` on `USharedQuestDefinition` with validation at `ServerRPC_AcceptQuest`. **Rejected** because group size is just a requirement — it belongs in `UnlockRequirements` as `URequirement_GroupSize` (defined in the party/group module). Putting it on the definition would duplicate logic that the requirement system already handles, and would make the quest system responsible for enforcing a constraint that the requirement system is designed to enforce.

### No `bAllowPassiveGroupContribution`
An earlier design had a flag on `USharedQuestDefinition` controlling whether group members who are NOT formally enrolled can still contribute tracker increments. **Removed** because this is a per-quest design decision that should not be a system-level boolean. It adds complexity with no clear ownership: who decides whether a random group member's kill counts for your quest? The tracker increment API is external; the bridge component in the game module decides what triggers what.

### `EQuestMemberRole` (Primary / Helper) Lives in Shared Extension Only
`FQuestRuntime` in the base system does not carry `MemberRole`. Role is only meaningful when `RetryAndAssist` lifecycle is in play and a player re-enters as a Helper after completing. Moving it to the shared extension keeps the base runtime lean.

---

## 4. Key Architectural Decisions

### State Machine Asset Reused as Stage Graph
**Decision:** `UQuestDefinition::StageGraph` is a `UStateMachineAsset`. Stage transitions are `FStateTransition` records with `UQuestTransitionRule` rules. No new stage definition class separate from the state machine.

**Alternative considered:** A custom `TArray<FQuestStageDefinition>` with a `NextStageTag` pointer and an optional branch list.

**Reason chosen:** The state machine already handles branching, non-interruptible states, AnyState transitions, and history. Reusing it means quest designers use the same graph editor as every other state machine in the game. `NextStageTag` would require a separate branching mechanism to be added later anyway. `UQuestTransitionRule : UTransitionRule` is the bridge — it evaluates a `URequirementList` against a `FRequirementContext*` passed as `ContextObject`. No new state machine code written.

### `UQuestRegistrySubsystem` as `UGameInstanceSubsystem`
**Decision:** The registry is a `UGameInstanceSubsystem`, not a `UWorldSubsystem`.

**Reason:** Quest definitions and the cadence reset clock are not world-specific. A `UWorldSubsystem` is destroyed and recreated on every world transition (loading screens, sublevel streams). This would drop the ref-counted definition cache and cadence timestamps on every transition, forcing redundant reloads and losing clock state. `UGameInstanceSubsystem` persists across world transitions for the duration of the game session.

### Completion Evaluated Imperatively on Tracker Increment
**Decision:** When a tracker reaches `EffectiveTarget`, `UQuestComponent::Server_IncrementTracker` immediately calls `EvaluateCompletionRequirementsNow`. There is no completion watcher that flushes on a coalescing delay.

**Reason:** The watcher flush has a configurable delay (default 0.5s). Stage advance and quest completion are time-sensitive player feedback moments — a 500ms delay before the completion animation or reward is unacceptable. The watcher's `RequirementEvent.Quest.TrackerUpdated` notification still fires for external systems (achievements, analytics) that can tolerate the delay. Two evaluation paths serve two different latency requirements.

### Watcher Used Only for Unlock Detection
**Decision:** `URequirementWatcherComponent` watches `UnlockRequirements` for candidate quests. It does not drive completion evaluation.

**Reason:** Completion has a single trigger (tracker reaching target). Routing it through the watcher would add latency and create two code paths that can both fire completion, requiring deduplication logic. Unlock detection is reactive by nature — the watcher is the right tool for it.

### Zero GMS Subscriptions in `UQuestComponent`
**Decision:** `UQuestComponent` never subscribes to any GMS event. All tracker increments arrive via the public `Server_IncrementTracker` API called by external integration layers.

**Reason:** If the quest system subscribed to `GameCoreEvent.Combat.MobKilled`, it would import the combat system. If it subscribed to inventory events, it would import the inventory system. The quest system's job is to track progress and evaluate requirements — not to know what game events cause progress. A thin bridge component in the game module owns the subscription and calls `Server_IncrementTracker`. This is the standard AAA pattern: systems expose increment APIs, wiring lives in the game layer.

### `FRequirementPayload` Injected into `FRequirementContext`
**Decision:** Tracker counters are injected into `FRequirementContext::PersistedData` as `FRequirementPayload` entries keyed by `QuestId`. Requirements read from the payload; they do not touch `FQuestRuntime` directly.

**Alternative considered:** A `URequirement_QuestTracker` that directly reads `UQuestComponent` to find the counter.

**Reason chosen:** Requirements must be stateless and have no coupling to the system that owns the data. If `URequirement_KillCount` imported `UQuestComponent`, the requirement system would depend on the quest system. Payload injection keeps requirements stateless and the requirement system dependency-free. The owning system (quest component) builds the payload; requirements read it blind.

### `CompletedQuestTags` as Fast Pre-Filter
**Decision:** Before any definition is loaded at `ServerRPC_AcceptQuest`, the system checks `CompletedQuestTags.HasTag(Def->QuestCompletedTag)`. This is an O(1) tag container lookup.

**Reason:** Loading a definition just to find out the quest is permanently closed is wasteful. The tag container check happens before the async definition load is triggered. For `SingleAttempt` quests this is the dominant rejection path — it fires before any I/O.

### Available Quests Not Persisted
**Decision:** Only `ActiveQuests` and `CompletedQuestTags` are saved. Available quests are recalculated by re-running watcher evaluation after login.

**Reason:** Available quests are entirely derivable from `CompletedQuestTags` + watcher evaluation of `UnlockRequirements`. Persisting them would mean the save file could diverge from the correct available set if quest definitions change between sessions. Recalculating on login is always correct and avoids a class of stale-data bugs entirely.

### `bEnabled` Removal Is Non-Destructive
**Decision:** Removing an active quest because `bEnabled = false` does NOT add `QuestCompletedTag`. The quest is silently removed from `ActiveQuests`.

**Reason:** If `QuestCompletedTag` were added, re-enabling the quest would still show as permanently closed for `SingleAttempt` and `RetryUntilComplete` quests. The purpose of `bEnabled` is a temporary kill switch, not a lifecycle decision. Non-destructive removal preserves the player's ability to attempt the quest again when it is re-enabled.

### `QuestId` Leaf Name Must Match Asset File Name
**Decision:** A quest with `QuestId = Quest.Id.TreasureHunt` must be in an asset named `TreasureHunt`. `UQuestRegistrySubsystem::ResolveQuestPath` uses the leaf tag name to look up the asset path from the Asset Manager ID list.

**Reason:** The Asset Manager enumerates all `QuestDefinition` primary assets at startup as `FPrimaryAssetId` records. The only stable identifier available without loading the asset is the asset file name. This convention makes tag-to-path resolution O(N assets) at startup and O(1) thereafter, without any additional mapping data structure. Validated by `UQuestDefinition::IsDataValid` at cook time.

### `LastCompletedTimestamp` Read as `int64` Directly
**Decision:** `URequirement_QuestCooldown` reads `FQuestRuntime::LastCompletedTimestamp` directly from `Context.QuestComponent`, not via `FRequirementPayload`.

**Alternative considered:** Storing the timestamp as a `float` in `FRequirementPayload::Floats`.

**Reason chosen:** Unix timestamps are `int64`. Casting to `float` loses precision around the year 2038 and produces wrong cooldown results for any timestamp beyond `2^24` seconds (~194 days from epoch). Reading directly via `Context.QuestComponent` preserves `int64` precision. `FRequirementPayload` intentionally omits `int64` support to keep it simple — timestamp data is the one case that bypasses payload injection.

### `UQuestComponent` Cached in `FRequirementContext`
**Decision:** `FRequirementContext` has a `TObjectPtr<UQuestComponent> QuestComponent` field set by `BuildRequirementContext`. Quest requirements use this instead of `FindComponentByClass`.

**Reason:** `FindComponentByClass` is O(N components) and is called on every requirement evaluation. With dozens of quest requirements evaluated per watcher flush and dozens of players, this is a measurable overhead. Caching the pointer in the context reduces each quest requirement evaluation to a null check and a pointer dereference. The `QuestComponent` field is acceptable in `FRequirementContext` because `UQuestComponent` is part of GameCore — it does not create a cross-module dependency.

---

## 5. Explicitly Rejected Features

| Feature | Why Rejected |
|---|---|
| `EGroupRequirement` / `MinGroupSize` / `MaxGroupSize` on `USharedQuestDefinition` | Group size is a requirement. It belongs in `UnlockRequirements` as `URequirement_GroupSize`, not duplicated on the definition. |
| `bAllowPassiveGroupContribution` on `USharedQuestDefinition` | No clear ownership. The game module's tracker bridge decides what triggers what. A system-level boolean here adds complexity without solving the problem cleanly. |
| GMS round-trip for `LeaderAccept` enrollment | No callback guarantee, no early-accept support, coordinator depends on a party-owned event tag. Replaced by `OnRequestGroupEnrollment` delegate with a `TFunction` callback. |
| `UStateMachineComponent` on `APlayerState` for quest stages | `UStateMachineComponent` drives side effects (`OnEnter`, `OnExit`, GMS broadcasts, persistence dirty). Quest stage state is a thin `FGameplayTag` in `FQuestRuntime`. Adding a full component for this would replicate data and fire unwanted events. `FindFirstPassingTransition` on the asset is the lightweight alternative. |
| `NextStageTag` field on `UQuestStageDefinition` | Would lose branching. The state machine transition graph already expresses branching; `NextStageTag` would require a separate branching mechanism. `UQuestTransitionRule` is the correct extension point. |
| Storing `LastCompletedTimestamp` in `FRequirementPayload` as `float` | Precision loss past `2^24` seconds. `int64` must be read directly from `FQuestRuntime`. |
| `UWorldSubsystem` for `UQuestRegistrySubsystem` | Destroyed on world transitions, losing the definition cache and cadence clock. `UGameInstanceSubsystem` persists correctly. |
| Per-category active quest limits | Adds complexity with unclear designer benefit. Single global `MaxActiveQuests` via `UQuestConfigDataAsset` is sufficient for initial implementation. Can be revisited. |
| Quest history / completed quest log in the quest system | The journal system owns quest history. Quest system stores only `CompletedQuestTags` (a tag container) — not descriptions, timestamps, or details of past quests. Journal subscribes to GMS events. |
| Reward granting in the quest system | The quest system emits `GameCoreEvent.Quest.Completed` with soft loot table references. The reward system subscribes and grants. Quest system never loads or distributes rewards. |
