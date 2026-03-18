# Integration Guide

**Sub-page of:** [Quest System](Quest%20System%20Overview.md)

This document explains how to integrate the Quest System into a game, what setup is required, which modules are responsible for which concerns, and how each external system interface works.

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
- Subscribe to any GMS events itself
- Know what triggers a tracker increment
- Grant rewards
- Write journal entries
- Render any UI
- Know about party/group systems (the shared quest extension handles that via `IGroupProvider`)

---

## Required Setup

### 1. GameCore Plugin

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "NetCore",
    "GameplayTags",
});
```

### 2. APlayerState Setup

```cpp
QuestComponent    = CreateDefaultSubobject<UQuestComponent>(TEXT("QuestComponent"));
// OR for shared quests:
QuestComponent    = CreateDefaultSubobject<USharedQuestComponent>(TEXT("QuestComponent"));
WatcherComponent  = CreateDefaultSubobject<URequirementWatcherComponent>(TEXT("WatcherComponent"));
```

### 3. Persistence Wiring

```cpp
PersistenceComp = CreateDefaultSubobject<UPersistenceRegistrationComponent>(
    TEXT("PersistenceComp"));
PersistenceComp->PersistenceTag =
    FGameplayTag::RequestGameplayTag(TEXT("Persistence.Entity.Player"));
```

**Persisted:** `ActiveQuests` (full `FQuestRuntime` array), `CompletedQuestTags`. 
**Not persisted:** Available quests (recalculated on login), watcher handles (re-registered in `BeginPlay`).

### 4. Quest Config Asset

Create a `UQuestConfigDataAsset` in your content folder and assign it to `UQuestComponent::QuestConfig`. Set `MaxActiveQuests` and any other tunables. This avoids hardcoded defaults in the component.

### 5. Asset Manager Configuration

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="QuestDefinition",
    AssetBaseClass=/Script/GameCore.QuestDefinition,
    bHasBlueprintClasses=False,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Quests"))
)
```

**Asset naming convention:** The asset file name must match the leaf node of `QuestDefinition::QuestId`. e.g. `QuestId = Quest.Id.TreasureHunt` → asset named `TreasureHunt`. This is validated by `UQuestDefinition::IsDataValid`.

### 6. Gameplay Tags

Copy tag entries from [File Structure](File%20Structure.md) into your `DefaultGameplayTags.ini`.

---

## Requirement Evaluation — Who Checks What

```
Unlock Requirements (quest becoming Available)
  ───────────────────────────────────────────────
  Evaluated by:  URequirementWatcherComponent (reactive, coalesced)
  Authority: ServerAuthoritative
    Server watcher evaluates → ClientRPC on pass (server is sole evaluator)
  Authority: ClientValidated
    Client watcher evaluates for UI feedback → ServerRPC_AcceptQuest on pass
    Server always re-evaluates before accepting
  Pre-filter:    CompletedQuestTags O(1) check, then bEnabled

Completion Requirements (stage advancing)
  ───────────────────────────────────────────────
  Trigger:  Server_IncrementTracker → tracker hits EffectiveTarget
            → EvaluateCompletionRequirementsNow (immediate, no watcher flush)
  Authority: ServerAuthoritative → server evaluates, notifies client
  Authority: ClientValidated → client fires ServerRPC_RequestValidation
             server re-evaluates authoritatively

Stage Transitions (branching)
  ───────────────────────────────────────────────
  Evaluated by:  UStateMachineAsset::FindFirstPassingTransition
  Rule type:     UQuestTransitionRule (evaluates URequirementList)
  Context:       FRequirementContext built by UQuestComponent
  Branching:     Multiple outgoing transitions from the same stage;
                 first passing rule wins.
```

---

## Serialization — What Saves What

| Data | Owner | Mechanism |
|---|---|---|
| `ActiveQuests` | `UQuestComponent` | `Serialize_Save` / `Serialize_Load` |
| `CompletedQuestTags` | `UQuestComponent` | `Serialize_Save` / `Serialize_Load` |
| Quest definitions | `UQuestRegistrySubsystem` | Asset Manager async load, not saved |
| Watcher handles | `URequirementWatcherComponent` | Not saved, re-registered on login |

Schema migration: increment `GetSchemaVersion()` and implement `Migrate()`. Unknown tags in saved data are skipped with a warning — no crash on content updates.

---

## Tracker Increments — Integration Layer Pattern

The quest system has zero GMS subscriptions. Use a bridge component:

```cpp
UCLASS(ClassGroup=(Game), meta=(BlueprintSpawnableComponent))
class UQuestTrackerBridge : public UActorComponent
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override
    {
        if (!HasAuthority()) return;
        auto& GMS = UGameCoreEventSubsystem::Get(this);
        GMS.RegisterListener(
            TAG_GameCoreEvent_Combat_MobKilled,
            this, &UQuestTrackerBridge::OnMobKilled);
    }
private:
    void OnMobKilled(const FMobKilledEventPayload& Payload)
    {
        if (Payload.KillerPlayerState != GetOwner()) return;
        UQuestComponent* QC =
            GetOwner<APlayerState>()->FindComponentByClass<UQuestComponent>();
        if (!QC) return;
        for (const FQuestRuntime& Runtime : QC->ActiveQuests.Items)
            for (const FQuestTrackerEntry& Tracker : Runtime.Trackers)
                if (IsKillTrackerForMob(Tracker.TrackerKey, Payload.MobTag))
                    QC->Server_IncrementTracker(
                        Runtime.QuestId, Tracker.TrackerKey, 1);
    }
    bool IsKillTrackerForMob(
        const FGameplayTag& TrackerKey, const FGameplayTag& MobTag) const
    { return TrackerKey.MatchesTag(MobTag); }
};
```

---

## Icons and Markers — UI Integration

`UQuestDefinition::QuestMarkerTag` → `UQuestMarkerDataAsset::GetIcon(Tag)` → soft texture loaded by UI. The quest system never loads textures.

`UQuestDefinition::Display` (`FQuestDisplayData`) holds localizable `FText` title, short/long description, difficulty enum, and a soft `UTexture2D`. Accessed by UI directly from the loaded definition.

`UQuestStageDefinition::StageObjectiveText` is broadcast in `FQuestStageChangedPayload` on `GameCoreEvent.Quest.StageStarted`. HUD subscribes and updates.

### Cooldown Countdown (UI)

The UI computes countdown without extra replication:
```
Cadence::None:   NextAvailable = LastCompletedTimestamp + CooldownSeconds
Cadence::Daily:  NextAvailable = GetLastDailyResetTimestamp() + 86400
Cadence::Weekly: NextAvailable = GetLastWeeklyResetTimestamp() + 604800
Remaining = NextAvailable - FDateTime::UtcNow().ToUnixTimestamp()
```

Subscribe to `GameCoreEvent.Quest.DailyReset` / `WeeklyReset` to refresh the display on reset.

---

## Interfaces to Other Systems

**Reward System:** Subscribes to `GameCoreEvent.Quest.Completed`. Payload carries soft `ULootTable` reference and `bIsHelperRun`. Reward system loads and grants. Quest system never loads the loot table.

**Journal System:** Subscribes to `Quest.Completed`, `Quest.Failed`, `Quest.Abandoned`. Quest system stores only `CompletedQuestTags`, no history.

**Party / Group System:** Communicates via `IGroupProvider` (GameCore). Implements `GetGroupSize`, `IsGroupLeader`, `GetGroupMembers`, `GetGroupActor` on `APlayerState`. Quest system never calls party RPCs or reads party state directly.

**Map / Compass System:** Reads `QuestMarkerTag` and `UQuestMarkerDataAsset` independently. Quest system provides no map API.

**Interaction System:** NPC `UInteractionComponent::EntryRequirements` can include `URequirement_QuestCompleted` or `URequirement_ActiveQuestCount`. Quest cooldown display text comes from `URequirement_QuestCooldown::FailReason` via `FRequirementResult`.

---

## Shared Quest Extension — Additional Setup

1. Replace `UQuestComponent` with `USharedQuestComponent` on `APlayerState`.
2. Implement `IGroupProvider` on `APlayerState` (or use `UGroupProviderDelegates`).
3. Add `USharedQuestCoordinator` to your group/party actor.
4. Group actor implements `IGroupProvider::GetGroupActor()` returning itself.
5. Bind `USharedQuestCoordinator::OnRequestGroupEnrollment` to your group invite flow.
6. Create `USharedQuestDefinition` assets for group quests.

If `APlayerState` does not implement `IGroupProvider`, `USharedQuestComponent` falls back to solo behavior silently.

---

## Checklist: Minimum Viable Integration

```
☐ GameCore plugin enabled
☐ UQuestComponent added to APlayerState
☐ URequirementWatcherComponent added to APlayerState
☐ UPersistenceRegistrationComponent added to APlayerState
☐ UQuestConfigDataAsset created and assigned to UQuestComponent::QuestConfig
☐ "QuestDefinition" primary asset type registered in DefaultGame.ini
☐ Gameplay tags from File Structure copied to DefaultGameplayTags.ini
☐ UQuestDefinition assets created in /Game/Quests/ (name = QuestId leaf)
☐ UQuestTrackerBridge (or equivalent) added to APlayerState
☐ UQuestMarkerDataAsset created and referenced by UI
☐ GMS listeners added in reward/journal systems for Quest.Completed
```

## Checklist: Shared Quest Extension

```
☐ USharedQuestComponent replaces UQuestComponent on APlayerState
☐ APlayerState implements IGroupProvider (or uses UGroupProviderDelegates)
☐ USharedQuestCoordinator added to group/party actor
☐ Group actor implements IGroupProvider::GetGroupActor() returning itself
☐ OnRequestGroupEnrollment delegate bound on USharedQuestCoordinator
☐ USharedQuestDefinition assets created for group quests
```
