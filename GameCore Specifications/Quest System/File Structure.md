# File Structure

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

---

## Module Location

The Quest System lives in the **game module**, not the GameCore plugin. It depends on GameCore but introduces no plugin-level coupling in reverse.

```
PirateGame/
└── Source/
    └── PirateGame/                        ← game module (or a dedicated Quest module)
        └── Quest/
            ├── Quest.Build.cs
            ├── Enums/
            │   └── QuestEnums.h                   ← all quest enums
            ├── Data/
            │   ├── QuestDefinition.h / .cpp
            │   ├── QuestStageDefinition.h / .cpp
            │   ├── QuestDisplayData.h
            │   ├── QuestProgressTrackerDef.h
            │   └── QuestMarkerDataAsset.h / .cpp
            ├── Runtime/
            │   └── QuestRuntime.h                 ← FQuestRuntime, FQuestTrackerEntry, FQuestRuntimeArray
            ├── Components/
            │   ├── QuestComponent.h / .cpp
            │   └── PartyQuestCoordinator.h / .cpp
            ├── Subsystems/
            │   └── QuestRegistrySubsystem.h / .cpp
            ├── Requirements/
            │   ├── Requirement_KillCount.h / .cpp
            │   ├── Requirement_QuestCompleted.h / .cpp
            │   ├── Requirement_QuestCooldown.h / .cpp
            │   ├── Requirement_GroupSize.h / .cpp
            │   └── Requirement_ActiveQuestCount.h
            └── Events/
                └── QuestEventPayloads.h           ← all GMS payload structs
```

---

## GameCore Plugin Changes

These files are modified or added in the **GameCore plugin**:

```
GameCore/
└── Source/
    └── GameCore/
        └── Requirements/
            ├── RequirementPayload.h            ← NEW: FRequirementPayload
            ├── RequirementContext.h             ← MODIFIED: add PersistedData field
            └── RequirementPersisted.h / .cpp    ← NEW: URequirement_Persisted
```

---

## Build.cs Dependencies

```csharp
// Quest/Quest.Build.cs  (or inside PirateGame.Build.cs)
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "GameCore",           // URequirement, URequirementList, UStateMachineAsset,
                          // URequirementWatcherComponent, UGameCoreEventSubsystem,
                          // IPersistableComponent
    "NetCore",            // FFastArraySerializer
});

PrivateDependencyModuleNames.AddRange(new string[]
{
    "DeveloperSettings",
});
```

---

## Config: DefaultGameplayTags.ini (Quest Module)

```ini
; ── Quest identity tags ─────────────────────────────────────────────────────────────────
+GameplayTagList=(Tag="Quest.Id",               DevComment="Namespace for quest identity tags")
+GameplayTagList=(Tag="Quest.Completed",        DevComment="Namespace for quest completion tags")
+GameplayTagList=(Tag="Quest.Payload",          DevComment="Namespace for FRequirementContext payload keys")
+GameplayTagList=(Tag="Quest.Counter",          DevComment="Namespace for counter keys within a payload")
+GameplayTagList=(Tag="Quest.Counter.LastCompleted", DevComment="Float: last completion Unix timestamp")

; ── Quest categories (expand as needed) ───────────────────────────────────────────────
+GameplayTagList=(Tag="Quest.Category.Story",     DevComment="Main story quests")
+GameplayTagList=(Tag="Quest.Category.SideQuest", DevComment="Optional side quests")
+GameplayTagList=(Tag="Quest.Category.Daily",     DevComment="Daily repeatable quests")
+GameplayTagList=(Tag="Quest.Category.Weekly",    DevComment="Weekly repeatable quests")
+GameplayTagList=(Tag="Quest.Category.Dungeon",   DevComment="Instanced dungeon quests")
+GameplayTagList=(Tag="Quest.Category.Event",     DevComment="Time-limited event quests")
+GameplayTagList=(Tag="Quest.Category.NPC",       DevComment="NPC-given quests")

; ── Quest marker types (add new markers here, map to icons in UQuestMarkerDataAsset) ──
+GameplayTagList=(Tag="Quest.Marker.MainStory",   DevComment="Gold star marker")
+GameplayTagList=(Tag="Quest.Marker.SideQuest",   DevComment="Silver marker")
+GameplayTagList=(Tag="Quest.Marker.Daily",       DevComment="Clock icon marker")
+GameplayTagList=(Tag="Quest.Marker.Dungeon",     DevComment="Skull marker")
+GameplayTagList=(Tag="Quest.Marker.Event",       DevComment="Event flag marker")
+GameplayTagList=(Tag="Quest.Marker.NPC",         DevComment="NPC exclamation marker")

; ── GMS event channels ──────────────────────────────────────────────────────────────────────
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

; ── Requirement invalidation events ───────────────────────────────────────────────────────
+GameplayTagList=(Tag="RequirementEvent.Quest.TrackerUpdated")
+GameplayTagList=(Tag="RequirementEvent.Quest.StageChanged")
+GameplayTagList=(Tag="RequirementEvent.Quest.Completed")
```

---

## Actor Setup

### APlayerState

```
APlayerState
  ├── UQuestComponent                    ← quest runtime, persistence
  └── URequirementWatcherComponent       ← unlock + completion watching
       (already required by other systems)
```

### Party Actor

```
APartyActor (game-specific)
  └── UPartyQuestCoordinator             ← shared tracker authority
```

---

## Key Constraints and Notes

- **Quest definitions are never instantiated per player.** `UQuestDefinition` is a shared read-only asset. Mutable state is always in `FQuestRuntime`.
- **`UStateMachineComponent` is NOT used for quest stages.** The quest system reads the `UStateMachineAsset` graph directly for stage/transition definitions and reuses `UStateNodeBase` / `UTransitionRule` as base classes for quest-specific subclasses. `UStateMachineComponent` is not added to `APlayerState` — the quest component drives stage transitions itself by tracking `CurrentStageTag` and evaluating transition rules from the asset.
- **`bReEvaluateOnly` trackers have no `FQuestTrackerEntry`.** They are not allocated in `FQuestRuntime::Trackers`. Requirements referencing them evaluate against live world state (inventory, tags, etc.) using non-persisted `URequirement` subclasses. The quest system does not store them.
- **`MaxActiveQuests` is enforced server-side only.** Client may display a capacity warning pre-accept by reading the replicated `ActiveQuests` count, but the server is the authoritative gate.
- **Abandon is always permitted** except for `GroupOnly` quests — the designer may add a `URequirement_GroupSize` check on the `UnlockRequirements` but abandon is not blocked at the system level.
- **`SingleAttempt` quests are permanently closed on the first fail or completion.** The `QuestCompletedTag` is added to `CompletedQuestTags` in both cases. Both are treated as terminal closed states in the unlock watcher pre-filter.
