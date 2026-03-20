# GameCore Changes

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

This page lists the additions made to the GameCore plugin to support the Quest System. All additions are generic improvements with no quest-specific knowledge — they are useful to any system.

---

## `IGroupProvider` + `UGroupProviderDelegates`

**Location:** [`GameCore Specifications/GameCore Core/GroupProvider.md`](../GameCore%20Core/GroupProvider.md)

A generic interface for reading group membership data. Implemented on `APlayerState` by the group/party system. Used by `USharedQuestComponent` to read group size, leader status, members, and the group actor without importing any concrete party type. `UGroupProviderDelegates` provides a delegate-backed default implementation for games that prefer loose binding.

---

## `FRequirementPayload` + `FRequirementContext::PersistedData`

**Location:** [Requirement System — Supporting Types](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`FRequirementPayload` carries keyed counters and floats injected into `FRequirementContext::PersistedData` before evaluation. `PersistedData` is a `TMap<FGameplayTag, FRequirementPayload>` — domain-namespaced to prevent key collisions between independent systems injecting into the same context. Full specification and design rationale in the Requirement System sub-pages.

---

## `URequirement_Persisted`

**Location:** [Requirement System — `URequirement` Base Class](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

Abstract `URequirement` subclass that seals `Evaluate()` and routes to `EvaluateWithPayload(Context, Payload)` after performing the `PersistedData` domain key lookup. Lives in `GameCore/Requirements/Requirement.h`. Fully specified in the Requirement System spec — no longer an interim reference.

---

## `UQuestTransitionRule` + `UQuestStateNode`

**Location:** [`GameCore Specifications/State Machine System`](../State%20Machine%20System%20318d261a36cf81859d55c7ad0bc3533a.md) — see **Extension Pattern** section.

`UQuestStateNode` extends `UStateNodeBase` with `bIsCompletionState` and `bIsFailureState` flags read by `UQuestComponent` after stage transition evaluation. `UQuestTransitionRule` extends `UTransitionRule` to evaluate a `URequirementList` against an `FRequirementContext*` passed as `ContextObject`. Enables full branching quest stage graphs without any new state machine code.

`UStateMachineAsset::FindFirstPassingTransition` is a pure utility method added to the asset class for systems (like `UQuestComponent`) that evaluate transitions without hosting a `UStateMachineComponent`.

---

## RequirementEvent Tags Added by the Quest Module

Defined in the Quest module's `DefaultGameplayTags.ini`, not in GameCore. Listed here for cross-reference.

```ini
+GameplayTagList=(Tag="RequirementEvent.Quest.TrackerUpdated")
+GameplayTagList=(Tag="RequirementEvent.Quest.StageChanged")
+GameplayTagList=(Tag="RequirementEvent.Quest.Completed")
+GameplayTagList=(Tag="Quest.Payload")
+GameplayTagList=(Tag="Quest.Counter")
+GameplayTagList=(Tag="Quest.Counter.LastCompleted")
```
