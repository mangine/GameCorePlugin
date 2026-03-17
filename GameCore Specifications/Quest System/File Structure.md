# File Structure

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

---

## Module Location

The Quest System lives in the **game module** (or a dedicated quest module), not the GameCore plugin.

```
PirateGame/
└── Source/
    └── PirateGame/
        └── Quest/
            ├── Quest.Build.cs
            ├── Enums/
            │   └── QuestEnums.h                        ← all quest enums incl. shared quest enums
            ├── Data/
            │   ├── QuestDefinition.h / .cpp            ← base solo quest definition
            │   ├── SharedQuestDefinition.h / .cpp      ← group extension
            │   ├── QuestStageDefinition.h / .cpp
            │   ├── QuestDisplayData.h
            │   ├── QuestProgressTrackerDef.h
            │   └── QuestMarkerDataAsset.h / .cpp
            ├── Runtime/
            │   └── QuestRuntime.h                      ← FQuestRuntime, FQuestTrackerEntry, FQuestRuntimeArray
            ├── Components/
            │   ├── QuestComponent.h / .cpp             ← solo quest component (base)
            │   ├── SharedQuestComponent.h / .cpp       ← group extension component
            │   └── SharedQuestCoordinator.h / .cpp     ← shared tracker authority on group actor
            ├── Subsystems/
            │   └── QuestRegistrySubsystem.h / .cpp
            ├── Requirements/
            │   ├── Requirement_QuestCompleted.h / .cpp
            │   ├── Requirement_QuestCooldown.h / .cpp
            │   └── Requirement_ActiveQuestCount.h
            ├── Events/
            │   └── QuestEventPayloads.h
            └── Integration/
                └── QuestTrackerBridge.h / .cpp         ← game-module bridge: subscribes to
                                                          combat/gathering GMS events, calls
                                                          UQuestComponent::Server_IncrementTracker
                                                          NOT part of the quest system itself
```

---

## GameCore Plugin Changes

```
GameCore/
└── Source/
    └── GameCore/
        ├── Interfaces/
        │   └── GroupProvider.h                     ← NEW: IGroupProvider interface
        └── Requirements/
            ├── RequirementPayload.h                ← NEW: FRequirementPayload
            ├── RequirementContext.h                ← MODIFIED: add PersistedData field
            ├── RequirementPersisted.h / .cpp       ← NEW: URequirement_Persisted
            └── RequirementWatcher.h / .cpp         ← MODIFIED: ContextBuilder on RegisterSet
```

---

## Build.cs Dependencies

```csharp
// Quest/Quest.Build.cs
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "GameCore",     // URequirement, URequirementList, UStateMachineAsset,
                    // URequirementWatcherComponent, UGameCoreEventSubsystem,
                    // IPersistableComponent, IGroupProvider
    "NetCore",      // FFastArraySerializer
});
```

---

## Actor Setup

### APlayerState

```
APlayerState
  ├── UQuestComponent                     ← solo quest runtime + persistence
  │   OR
  ├── USharedQuestComponent               ← drop-in replacement; inherits UQuestComponent
  ├── URequirementWatcherComponent        ← always required (existing GameCore component)
  └── implements IGroupProvider           ← optional; required only for shared quest group
                                              validation. Implemented by the party system.
```

### Group Actor (party, ship, squad — game-specific)

```
AGroupActor (game-specific)
  ├── USharedQuestCoordinator             ← shared tracker authority
  └── implements IGroupProvider           ← provides GetGroupSize, IsGroupLeader,
                                              GetGroupMembers to coordinator
```

---

## Config: DefaultGameplayTags.ini

```ini
; ── Identity ──────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="Quest.Id")
+GameplayTagList=(Tag="Quest.Completed")
+GameplayTagList=(Tag="Quest.Payload")
+GameplayTagList=(Tag="Quest.Counter")
+GameplayTagList=(Tag="Quest.Counter.LastCompleted")

; ── Categories ────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="Quest.Category.Story")
+GameplayTagList=(Tag="Quest.Category.SideQuest")
+GameplayTagList=(Tag="Quest.Category.Daily")
+GameplayTagList=(Tag="Quest.Category.Weekly")
+GameplayTagList=(Tag="Quest.Category.Dungeon")
+GameplayTagList=(Tag="Quest.Category.Event")
+GameplayTagList=(Tag="Quest.Category.NPC")

; ── Markers ──────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="Quest.Marker.MainStory")
+GameplayTagList=(Tag="Quest.Marker.SideQuest")
+GameplayTagList=(Tag="Quest.Marker.Daily")
+GameplayTagList=(Tag="Quest.Marker.Dungeon")
+GameplayTagList=(Tag="Quest.Marker.Event")
+GameplayTagList=(Tag="Quest.Marker.NPC")

; ── GMS event channels ─────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="GameCoreEvent.Quest.Started")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Completed")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Failed")
+GameplayTagList=(Tag="GameCoreEvent.Quest.Abandoned")
+GameplayTagList=(Tag="GameCoreEvent.Quest.BecameAvailable")
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageStarted")
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageCompleted")
+GameplayTagList=(Tag="GameCoreEvent.Quest.StageFailed")
+GameplayTagList=(Tag="GameCoreEvent.Quest.TrackerUpdated")
+GameplayTagList=(Tag="GameCoreEvent.Quest.DailyReset")
+GameplayTagList=(Tag="GameCoreEvent.Quest.WeeklyReset")
+GameplayTagList=(Tag="GameCoreEvent.Quest.PartyInvite")
+GameplayTagList=(Tag="GameCoreEvent.Quest.MemberLeft")

; ── Requirement invalidation events ───────────────────────────────────────────────
+GameplayTagList=(Tag="RequirementEvent.Quest.TrackerUpdated")
+GameplayTagList=(Tag="RequirementEvent.Quest.StageChanged")
+GameplayTagList=(Tag="RequirementEvent.Quest.Completed")
```

---

## Key Constraints and Notes

- **`UStateMachineComponent` is NOT added to `APlayerState`.** The quest component reads `UStateMachineAsset` directly for stage transition logic. `FQuestRuntime::CurrentStageTag` is the runtime state.
- **`bReEvaluateOnly` trackers have no `FQuestTrackerEntry`.** They are not allocated in `FQuestRuntime::Trackers`. Requirements reading them evaluate live world state each time.
- **`UQuestComponent` has zero GMS subscriptions.** All tracker increments arrive via `Server_IncrementTracker` called by external integration layers.
- **`USharedQuestComponent` also has zero GMS subscriptions.** The integration layer calls `OnGroupMemberTrackerContribution` or `Server_IncrementTracker` directly.
- **`bEnabled = false` removal is non-destructive.** No `QuestCompletedTag` is added. Re-enabling a quest makes it available again on next login.
- **`USharedQuestDefinition` registers under the same `"QuestDefinition"` asset type.** The registry loads both identically. `USharedQuestComponent` upcasts; base `UQuestComponent` never does.
- **`MaxActiveQuests` is enforced server-side only.** Client reads `ActiveQuests.Items.Num()` for pre-validation UI hints.
