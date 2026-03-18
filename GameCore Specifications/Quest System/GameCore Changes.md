# GameCore Changes

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

This page lists the additions made to the GameCore plugin to support the Quest System. All additions are generic improvements with no quest-specific knowledge — they are useful to any system.

Each item links to where it is fully specified within the GameCore specifications.

---

## Changes Made

### `IGroupProvider` + `UGroupProviderDelegates`

**Location:** [`GameCore Specifications/GameCore Core/GroupProvider.md`](../GameCore%20Core/GroupProvider.md)

A generic interface for reading group membership data. Implemented on `APlayerState` by the group/party system. Used by `USharedQuestComponent` to read group size, leader status, members, and the group actor without importing any concrete party type. `UGroupProviderDelegates` provides a delegate-backed default implementation for games that prefer loose binding.

---

### `FRequirementPayload` + `FRequirementContext` additions

**Location:** [`GameCore Specifications/Requirement System`](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`FRequirementPayload` is a new USTRUCT carrying keyed counters and floats injected into `FRequirementContext::PersistedData` before evaluation. `FRequirementContext` gains two new fields: `PersistedData` (the payload map) and `QuestComponent` (cached pointer for quest requirement fast-path). Full spec in the Requirement System overview.

---

### `URequirement_Persisted` (abstract base class)

> **Status: Not yet specified in the Requirement System spec files.**
>
> `URequirement_Persisted` is an abstract `URequirement` subclass that seals `Evaluate()` and routes evaluation through `EvaluateWithPayload(Context, Payload)` after performing the `PersistedData` key lookup. It lives in `GameCore/Source/GameCore/Requirements/RequirementPersisted.h`.
>
> The class definition exists in [`GameCore Specifications/Quest System/GameCore Changes (legacy).md`] as an interim reference. It must be moved into the Requirement System sub-page (`URequirement — Base Class`) when that file is updated.

---

### `UQuestTransitionRule` + `UQuestStateNode`

**Location:** [`GameCore Specifications/State Machine System`](../State%20Machine%20System%20318d261a36cf81859d55c7ad0bc3533a.md) — see **Extension Pattern** section.

`UQuestStateNode` extends `UStateNodeBase` with `bIsCompletionState` and `bIsFailureState` flags read by `UQuestComponent` after stage transition evaluation. `UQuestTransitionRule` extends `UTransitionRule` to evaluate a `URequirementList` against an `FRequirementContext*` passed as `ContextObject`. Enables full branching quest stage graphs without any new state machine code.

`UStateMachineAsset::FindFirstPassingTransition` is a pure utility method added to the asset class for systems (like `UQuestComponent`) that evaluate transitions without hosting a `UStateMachineComponent`.

---

## RequirementEvent Tags Added by the Quest Module

These tags are defined in the Quest module's `DefaultGameplayTags.ini`, not in GameCore. They are listed here for cross-reference.

```ini
+GameplayTagList=(Tag="RequirementEvent.Quest.TrackerUpdated")
+GameplayTagList=(Tag="RequirementEvent.Quest.StageChanged")
+GameplayTagList=(Tag="RequirementEvent.Quest.Completed")
+GameplayTagList=(Tag="Quest.Payload")
+GameplayTagList=(Tag="Quest.Counter")
+GameplayTagList=(Tag="Quest.Counter.LastCompleted")
```
