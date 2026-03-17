# Integration Guide

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

This document explains how to integrate the Quest System into a game, what setup is required, which modules are responsible for which concerns, and how each external system interface works. It is written for a developer setting up the system for the first time.

---

## What the Quest System Does and Does Not Do

**Does:**
- Define quest data (stages, requirements, lifecycle, display metadata)
- Track per-player active quest state and progress counters
- Evaluate unlock and completion requirements reactively
- Replicate quest state to the owning client
- Persist active quest state and completed quest tags
- Emit GMS events for downstream systems to react to
- Provide a public `Server_IncrementTracker` API for external systems to drive progress

**Does not:**
- Subscribe to any GMS events itself (combat, inventory, etc.)
- Know what triggers a tracker increment
- Grant rewards (emits a completed event with a soft loot table reference)
- Write journal entries
- Render any UI
- Know about party/group systems (the shared quest extension handles that via `IGroupProvider`)

---

## Required Setup

### 1. GameCore Plugin

The quest system depends on GameCore. Ensure the plugin is enabled and the following modules are in your `Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",   // URequirement, URequirementList, UStateMachineAsset,
                  // URequirementWatcherComponent, UGameCoreEventSubsystem,
                  // IPersistableComponent, IGroupProvider
    "NetCore",    // FFastArraySerializer
    "GameplayTags",
});
```

### 2. APlayerState Setup

Every player must have these two components on `APlayerState`:

```cpp
// In AMyPlayerState constructor:
QuestComponent = CreateDefaultSubobject<UQuestComponent>(TEXT("QuestComponent"));
// OR, if using shared quests:
QuestComponent = CreateDefaultSubobject<USharedQuestComponent>(TEXT("QuestComponent"));

// Required by GameCore — likely already present:
WatcherComponent = CreateDefaultSubobject<URequirementWatcherComponent>(TEXT("WatcherComponent"));
```

`URequirementWatcherComponent` must be present before `UQuestComponent::BeginPlay` runs. Order of `CreateDefaultSubobject` calls does not matter — both exist by the time `BeginPlay` fires.

### 3. Persistence Wiring

`UQuestComponent` implements `IPersistableComponent`. For persistence to activate, the owning actor must also have `UPersistenceRegistrationComponent`:

```cpp
PersistenceComp = CreateDefaultSubobject<UPersistenceRegistrationComponent>(
    TEXT("PersistenceComp"));
PersistenceComp->PersistenceTag =
    FGameplayTag::RequestGameplayTag(TEXT("Persistence.Entity.Player"));
```

When `UPersistenceRegistrationComponent` is present, `UQuestComponent` automatically participates in save/load cycles via `Serialize_Save` / `Serialize_Load`. No additional wiring needed.

**What is persisted:**
- `ActiveQuests` — full `FQuestRuntime` array including tracker values and stage
- `CompletedQuestTags` — tag container of permanently closed quests

**What is NOT persisted (recalculated on login):**
- Available quests — re-evaluated by re-running unlock watcher registration
- Watcher handles — re-registered in `BeginPlay` after load

### 4. Asset Manager Configuration

Quest definitions are `UPrimaryDataAsset` subclasses loaded on demand. Register the asset type in `DefaultGame.ini`:

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="QuestDefinition",
    AssetBaseClass=/Script/PirateGame.QuestDefinition,
    bHasBlueprintClasses=False,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Quests"))
)
```

This covers both `UQuestDefinition` and `USharedQuestDefinition` since both use `"QuestDefinition"` as their primary asset type.

### 5. Gameplay Tags

Copy the tag entries from [File Structure — DefaultGameplayTags.ini](File%20Structure.md) into your project's `DefaultGameplayTags.ini`. Tags must be present before any quest definition assets are loaded.

---

## Requirement Evaluation — Who Checks What

This is the most important thing to understand about the quest system's architecture.

```
Unlock Requirements (quest becoming Available)
  ───────────────────────────────────────────────
  Evaluated by:  URequirementWatcherComponent (reactive, coalesced flush)
  Authority:     Driven by UQuestDefinition::CheckAuthority
    ServerAuthoritative → watcher runs on SERVER only
                          server fires ClientRPC on pass
    ClientValidated     → watcher runs on SERVER + OWNING CLIENT
                          client fires ServerRPC on pass
                          server re-evaluates; never trusts client result
  Context:       BuildRequirementContext() with empty payload
                 (unlock requirements do not read tracker data)
  Pre-filter:    CompletedQuestTags checked first (O(1) tag lookup)
                 bEnabled checked second (skips disabled quests entirely)

Completion Requirements (stage advancing)
  ───────────────────────────────────────────────
  Evaluated by:  UQuestComponent directly (imperative, on tracker increment)
  Trigger:       Server_IncrementTracker → tracker reaches EffectiveTarget
                 → EvaluateCompletionRequirementsNow(QuestId)
  Authority:     ServerAuthoritative → server evaluates, notifies client
                 ClientValidated     → client fires ServerRPC_RequestValidation
                                       server re-evaluates authoritatively
  Context:       BuildRequirementContext() WITH tracker payload injected
                 FRequirementPayload keyed by QuestId, counters from FQuestRuntime
```

### The ContextBuilder Pattern

When `UQuestComponent` registers completion requirements with the watcher, it supplies a `ContextBuilder` lambda that injects the current tracker values into `FRequirementContext::PersistedData` before every evaluation. This is how `URequirement_Persisted` subclasses (like `URequirement_KillCount`) can read live progress without polling:

```
Watcher flush triggered by RequirementEvent.Quest.TrackerUpdated
  → For each dirty set: call ContextBuilder(Ctx)
      ContextBuilder reads FQuestRuntime::Trackers
      Injects FRequirementPayload{Counters: {Quest.Counter.Kill: 2}} into Ctx
  → URequirement_KillCount::EvaluateWithPayload(Ctx, Payload)
      Reads Payload.Counters[CounterTag] = 2
      Compares to RequiredKills = 3 → Fail
```

---

## Serialization — What Saves What

| Data | Owner | Mechanism | Notes |
|---|---|---|---|
| `ActiveQuests` | `UQuestComponent` | `IPersistableComponent::Serialize_Save` | Full `FQuestRuntime` array, tags serialized as `FName` |
| `CompletedQuestTags` | `UQuestComponent` | `IPersistableComponent::Serialize_Save` | Tag name array |
| Quest definitions | `UQuestRegistrySubsystem` | Asset Manager async load | Not saved — loaded on demand, unloaded when no active players |
| Watcher handles | `URequirementWatcherComponent` | Not persisted | Re-registered in `BeginPlay` after load |
| Available quests | None | Not persisted | Recalculated by unlock watcher on login |

The persistence system calls `Serialize_Save` / `Serialize_Load` automatically via `UPersistenceRegistrationComponent`. The game module does not need to call these manually.

**Schema versioning:** `UQuestComponent::GetSchemaVersion()` returns `1`. Increment this and implement `Migrate()` if the serialization layout changes after shipping.

---

## Tracker Increments — Integration Layer Pattern

The quest system has zero GMS subscriptions. Progress is driven entirely by external calls to `UQuestComponent::Server_IncrementTracker`. The recommended integration pattern is a thin bridge component on `APlayerState`:

```cpp
// Game module: QuestTrackerBridge.h
UCLASS(ClassGroup=(Game), meta=(BlueprintSpawnableComponent))
class UQuestTrackerBridge : public UActorComponent
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override
    {
        if (!HasAuthority()) return;

        // Subscribe to combat events
        auto& GMS = UGameCoreEventSubsystem::Get(this);
        GMS.RegisterListener(
            TAG_GameCoreEvent_Combat_MobKilled,
            this, &UQuestTrackerBridge::OnMobKilled);
    }

private:
    void OnMobKilled(const FMobKilledEventPayload& Payload)
    {
        // Only handle kills by this player
        if (Payload.KillerPlayerState != GetOwner()) return;

        UQuestComponent* QC =
            GetOwner<APlayerState>()->FindComponentByClass<UQuestComponent>();
        if (!QC) return;

        // Iterate active quests and increment any matching kill trackers.
        // TrackerKey matching is game-specific — depends on your mob tag scheme.
        for (const FQuestRuntime& Runtime : QC->ActiveQuests.Items)
        {
            for (const FQuestTrackerEntry& Tracker : Runtime.Trackers)
            {
                if (IsKillTrackerForMob(Tracker.TrackerKey, Payload.MobTag))
                {
                    QC->Server_IncrementTracker(
                        Runtime.QuestId, Tracker.TrackerKey, 1);
                }
            }
        }
    }

    bool IsKillTrackerForMob(
        const FGameplayTag& TrackerKey, const FGameplayTag& MobTag) const
    {
        // Game-specific: e.g. Quest.Counter.Kill.Skeleton matches MobTag.Skeleton
        // Implementation depends on your tracker key naming convention.
        return TrackerKey.MatchesTag(MobTag);
    }
};
```

Add `UQuestTrackerBridge` to `APlayerState` alongside `UQuestComponent`. This pattern keeps the quest system and combat system fully decoupled — neither imports the other.

---

## Icons and Markers — UI Integration

### Quest Marker Icons

`UQuestDefinition::QuestMarkerTag` is a `FGameplayTag` (e.g. `Quest.Marker.MainStory`). Map it to a texture in `UQuestMarkerDataAsset`:

```
Content/UI/Quest/DA_QuestMarkers (UQuestMarkerDataAsset)
  MarkerIcons:
    Quest.Marker.MainStory  → T_Icon_MainStory
    Quest.Marker.SideQuest  → T_Icon_SideQuest
    Quest.Marker.Daily      → T_Icon_Daily
    Quest.Marker.Dungeon    → T_Icon_Skull
```

The UI widget reads `QuestDefinition->QuestMarkerTag`, calls `MarkerDataAsset->GetIcon(Tag)`, and loads the soft texture reference on demand. The quest system never loads textures.

### Display Data

`UQuestDefinition::Display` (`FQuestDisplayData`) contains:
- `Title` — `FText`, localizable, shown in quest log header
- `ShortDescription` — `FText`, one-line summary for tracker HUD
- `LongDescription` — `FText`, full description for quest log detail view
- `Difficulty` — `EQuestDifficulty`, shown as star/skull rating
- `QuestImage` — soft `UTexture2D`, loaded by UI on demand

None of these are loaded by the quest system at runtime. The UI accesses them directly from the loaded `UQuestDefinition`.

### Stage Objective Text

`UQuestStageDefinition::StageObjectiveText` (`FText`) is broadcast in `FQuestStageChangedPayload` when a stage becomes active. The HUD subscribes to `GameCoreEvent.Quest.StageStarted` and updates the objective tracker display from the payload.

---

## Interfaces to Other Systems

### Reward System

The quest system does NOT grant rewards. On completion it emits `GameCoreEvent.Quest.Completed` with `FQuestCompletedPayload` containing:
- `RewardTable` — soft reference to `ULootTable` (first-time or repeating based on role)
- `bIsHelperRun` — whether this was a helper run
- `MemberRole` — `Primary` or `Helper`

The reward system subscribes to this event, loads the loot table, and grants rewards.

### Journal System

The quest system emits `Quest.Completed`, `Quest.Failed`, `Quest.Abandoned` with full payload. The journal system subscribes and writes its own log entries. The quest system only stores `CompletedQuestTags` (a tag container) — not quest history or completion details.

### Party / Group System

The shared quest extension communicates with the group system exclusively via `IGroupProvider` (GameCore). The party system implements this interface on `APlayerState`:

```
IGroupProvider (GameCore interface)
  GetGroupSize()    → used for tracker ScalingMultiplier calculation
  IsGroupLeader()   → used for LeaderAccept validation
  GetGroupMembers() → used for passive tracker contribution fan-out
```

The quest system never calls party RPCs, never reads party actor state directly, and has no import dependency on the party module.

### Map / Compass System

The map system reads `UQuestDefinition::QuestMarkerTag` and `UQuestMarkerDataAsset` independently. The quest system provides no map integration itself — it exposes the tag and the player's `ActiveQuests` via the replicated component, and the map system reads those.

### Interaction System

QuestNPC interactions are handled by `UInteractionComponent` on the NPC actor. The interaction's `EntryRequirements` can include `URequirement_QuestCompleted` or `URequirement_ActiveQuestCount` to gate interactions based on quest state. The quest system does not know about interaction entries.

Cooldown display (e.g. “Quest resets in 4h”) is surfaced via `URequirement_QuestCooldown::FailReason` text, which is passed through the requirement system's `FRequirementResult` and displayed by the interaction UI. No special quest system integration required.

---

## Shared Quest Extension — Additional Setup

To enable shared quests:

1. Replace `UQuestComponent` with `USharedQuestComponent` on `APlayerState`.
2. Implement `IGroupProvider` on `APlayerState` (delegating to your party component).
3. Add `USharedQuestCoordinator` to your group/party actor.
4. Create `USharedQuestDefinition` assets instead of `UQuestDefinition` for group quests (both types can coexist in the registry).

If `APlayerState` does not implement `IGroupProvider`, `USharedQuestComponent` falls back to solo behavior — it does not crash or assert. This means you can drop in `USharedQuestComponent` before the party system is ready and everything still works.

---

## Checklist: Minimum Viable Integration

```
☐ GameCore plugin enabled
☐ UQuestComponent added to APlayerState
☐ URequirementWatcherComponent added to APlayerState
☐ UPersistenceRegistrationComponent added to APlayerState (for save/load)
☐ "QuestDefinition" primary asset type registered in DefaultGame.ini
☐ Gameplay tags from File Structure copied to DefaultGameplayTags.ini
☐ At least one UQuestDefinition asset created in /Game/Quests/
☐ UQuestTrackerBridge (or equivalent) added to APlayerState for tracker events
☐ UQuestMarkerDataAsset created and referenced by UI for marker icons
☐ GMS listeners added in reward/journal systems for Quest.Completed event
```

## Checklist: Shared Quest Extension

```
☐ USharedQuestComponent replaces UQuestComponent on APlayerState
☐ APlayerState implements IGroupProvider
☐ USharedQuestCoordinator added to group/party actor
☐ Group actor implements IGroupProvider
☐ USharedQuestDefinition assets created for group quests
```
