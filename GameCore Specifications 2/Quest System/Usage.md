# Quest System — Usage Guide

This document covers how to set up, configure, and use the Quest System from a game module perspective.

---

## Minimum Viable Setup

### 1. Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore", "NetCore", "GameplayTags",
});
```

### 2. APlayerState

```cpp
// AMyPlayerState.h
UPROPERTY(VisibleAnywhere)
TObjectPtr<UQuestComponent> QuestComponent;

UPROPERTY(VisibleAnywhere)
TObjectPtr<URequirementWatcherComponent> WatcherComponent;

UPROPERTY(VisibleAnywhere)
TObjectPtr<UPersistenceRegistrationComponent> PersistenceComp;

// AMyPlayerState.cpp
QuestComponent   = CreateDefaultSubobject<UQuestComponent>(TEXT("QuestComponent"));
WatcherComponent = CreateDefaultSubobject<URequirementWatcherComponent>(TEXT("WatcherComponent"));
PersistenceComp  = CreateDefaultSubobject<UPersistenceRegistrationComponent>(TEXT("PersistenceComp"));
PersistenceComp->PersistenceTag = FGameplayTag::RequestGameplayTag(TEXT("Persistence.Entity.Player"));
```

### 3. Quest Config Asset

Create `DA_QuestConfig` (type `UQuestConfigDataAsset`) and assign it to `QuestComponent->QuestConfig`. Set `MaxActiveQuests`.

### 4. Asset Manager (DefaultGame.ini)

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="QuestDefinition",
    AssetBaseClass=/Script/YourGame.QuestDefinition,
    bHasBlueprintClasses=False,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Quests"))
)
```

---

## Creating a Quest

### Step 1: Create a `UQuestStageDefinition` per stage

Each stage defines its completion conditions and trackers. Set `bIsCompletionState = true` on the final success stage, `bIsFailureState = true` on failure states.

```cpp
// Stage 1: Kill 10 Crabs (counter-based)
// TrackerKey: Quest.Counter.Kill.Crab, TargetValue: 10
// CompletionRequirements: URequirementList that passes when tracker >= 10

// Stage 2: Deliver item (re-evaluate only)
// bReEvaluateOnly: true — no counter, CompletionRequirements checks inventory live
```

### Step 2: Create a `UStateMachineAsset` for the stage graph

- Each state tag corresponds to a `UQuestStageDefinition::StageTag`.
- Add `UQuestTransitionRule` transitions between states.
- Each rule holds a `URequirementList` evaluated at transition time.
- Use `UQuestStateNode` for states — set `bIsCompletionState` / `bIsFailureState` on terminal states.

### Step 3: Create a `UQuestDefinition` asset

Asset must be named after the leaf tag. e.g. `QuestId = Quest.Id.TreasureHunt` → asset name `TreasureHunt`.

```
QuestId:              Quest.Id.TreasureHunt
QuestCompletedTag:    Quest.Completed.TreasureHunt
bEnabled:             true
Lifecycle:            RetryUntilComplete
CheckAuthority:       ClientValidated
ResetCadence:         None
StageGraph:           <your UStateMachineAsset>
Stages:               [Stage1, Stage2]
UnlockRequirements:   <URequirementList — e.g. URequirement_QuestCompleted for a prerequisite>
FirstTimeRewardTable: <soft ref to ULootTable>
Display:              Title, ShortDescription, Difficulty, soft texture
```

---

## Triggering Tracker Progress

The quest system has zero GMS subscriptions. Create a bridge component on `APlayerState` that subscribes to game events and calls `Server_IncrementTracker`.

```cpp
// UQuestTrackerBridge.h
UCLASS(ClassGroup=(Game), meta=(BlueprintSpawnableComponent))
class UQuestTrackerBridge : public UActorComponent
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
private:
    void OnMobKilled(const FMobKilledPayload& Payload);
};

// UQuestTrackerBridge.cpp
void UQuestTrackerBridge::BeginPlay()
{
    Super::BeginPlay();
    if (!GetOwner()->HasAuthority()) return;

    UGameCoreEventBus::Get(this).RegisterListener(
        TAG_GameCoreEvent_Combat_MobKilled,
        this, &UQuestTrackerBridge::OnMobKilled);
}

void UQuestTrackerBridge::OnMobKilled(const FMobKilledPayload& Payload)
{
    // Only process kills from this player.
    if (Payload.KillerPlayerState != GetOwner()) return;

    UQuestComponent* QC =
        GetOwner<APlayerState>()->FindComponentByClass<UQuestComponent>();
    if (!QC) return;

    // Walk active quests and increment matching tracker keys.
    for (const FQuestRuntime& Runtime : QC->ActiveQuests.Items)
    {
        for (const FQuestTrackerEntry& Tracker : Runtime.Trackers)
        {
            // Convention: tracker key tag hierarchy matches mob tag hierarchy.
            // Quest.Counter.Kill.Crab matches GameCoreTag.Mob.Crab via HasTag.
            if (Tracker.TrackerKey.MatchesTag(Payload.MobTypeTag))
            {
                QC->Server_IncrementTracker(Runtime.QuestId, Tracker.TrackerKey, 1);
            }
        }
    }
}
```

Add to `APlayerState`:

```cpp
TrackerBridge = CreateDefaultSubobject<UQuestTrackerBridge>(TEXT("QuestTrackerBridge"));
```

---

## Listening to Quest Events

All quest events are GMS channels. Subscribe wherever you need to react (reward system, journal, achievements, UI).

```cpp
// Reward System subscribes to Quest.Completed:
UGameCoreEventBus::Get(this).RegisterListener(
    TAG_GameCoreEvent_Quest_Completed,
    this, &URewardSystem::OnQuestCompleted);

void URewardSystem::OnQuestCompleted(const FQuestCompletedPayload& Payload)
{
    if (Payload.RewardTable.IsNull()) return;
    // Load the loot table and grant. Quest system never loads it.
    ULootTable* Table = Payload.RewardTable.LoadSynchronous(); // or async
    GrantLoot(Payload.PlayerState.Get(), Table, Payload.bIsHelperRun);
}
```

---

## Quest UI — Reading State

```cpp
// Get all active quests:
const FQuestRuntimeArray& Active = QuestComponent->ActiveQuests;

for (const FQuestRuntime& Runtime : Active.Items)
{
    FGameplayTag QuestId = Runtime.QuestId;
    FGameplayTag StageTag = Runtime.CurrentStageTag;

    // Progress bars:
    for (const FQuestTrackerEntry& Tracker : Runtime.Trackers)
    {
        float Progress = (float)Tracker.CurrentValue / Tracker.EffectiveTarget;
        // Update UI...
    }
}

// Check if a specific quest is completed:
bool bDone = QuestComponent->CompletedQuestTags.HasTag(
    FGameplayTag::RequestGameplayTag(TEXT("Quest.Completed.TreasureHunt")));
```

### Cooldown Countdown

```cpp
// Cadence::None:
int64 NextAvailable = Runtime.LastCompletedTimestamp + CooldownSeconds;

// Cadence::Daily:
int64 NextAvailable = Registry->GetLastDailyResetTimestamp() + 86400;

// Cadence::Weekly:
int64 NextAvailable = Registry->GetLastWeeklyResetTimestamp() + 604800;

int64 Remaining = NextAvailable - FDateTime::UtcNow().ToUnixTimestamp();
// Subscribe to GameCoreEvent.Quest.DailyReset / WeeklyReset to refresh display.
```

---

## Force Complete / Fail (Admin / Debug)

```cpp
// Server-side only:
QuestComponent->Server_ForceCompleteQuest(
    FGameplayTag::RequestGameplayTag(TEXT("Quest.Id.TreasureHunt")));

QuestComponent->Server_ForceFailQuest(
    FGameplayTag::RequestGameplayTag(TEXT("Quest.Id.TreasureHunt")));
```

---

## Abandoning a Quest

Player triggers abandonment (client → server RPC):

```cpp
// Client calls:
QuestComponent->ServerRPC_AbandonQuest(
    FGameplayTag::RequestGameplayTag(TEXT("Quest.Id.TreasureHunt")));
```

Server removes the quest from `ActiveQuests` and emits `GameCoreEvent.Quest.Abandoned`. For `SingleAttempt` quests abandoning does **not** add `QuestCompletedTag` — the player may re-accept.

---

## Shared Quests (Optional Extension)

### Setup

```cpp
// Replace UQuestComponent on APlayerState:
QuestComponent = CreateDefaultSubobject<USharedQuestComponent>(TEXT("QuestComponent"));

// APlayerState implements IGroupProvider:
class AMyPlayerState : public APlayerState, public IGroupProvider { ... };

// Add USharedQuestCoordinator to your group actor:
QuestCoordinator = CreateDefaultSubobject<USharedQuestCoordinator>(TEXT("QuestCoordinator"));

// Bind enrollment delegate:
QuestCoordinator->OnRequestGroupEnrollment.BindUObject(
    PartyComponent, &UMyPartyComponent::HandleQuestEnrollmentRequest);
```

### Creating a Shared Quest Definition

Use `USharedQuestDefinition`. Set `AcceptanceMode` and per-tracker `ScalingMultiplier`. Group size restrictions belong in `UnlockRequirements` as `URequirement_GroupSize` — not on the definition directly.

### Enrollment Delegate Implementation

```cpp
void UMyPartyComponent::HandleQuestEnrollmentRequest(
    const FGameplayTag& QuestId,
    const TArray<APlayerState*>& InvitedMembers,
    float GraceSeconds,
    TFunction<void(const TArray<APlayerState*>&)> OnEnrollmentResolved)
{
    // Send UI invites, start grace timer.
    // Call OnEnrollmentResolved(ConfirmedMembers) when all accept or timer expires.
    // Call with empty array if all decline.
    MyInviteFlow.Start(QuestId, InvitedMembers, GraceSeconds,
        MoveTemp(OnEnrollmentResolved));
}
```

---

## Interaction System Integration

Quest giver NPCs use `UInteractionComponent`. Gate interaction with quest requirements:

```cpp
// On the NPC's UInteractionEntryDataAsset:
// EntryRequirements: URequirementList
//   - URequirement_QuestCompleted(Quest.Completed.PrereqQuest)  <- prerequisite
//   - URequirement_ActiveQuestCount(MaxAllowed=20)              <- capacity check

// The FailureReason from URequirement_QuestCooldown is shown in interaction UI:
// "Available in 3600s" or "Quest not yet reset."
```

---

## Checklist: Minimum Viable Integration

```
☐ GameCore plugin enabled, Quest module added to Build.cs
☐ UQuestComponent added to APlayerState
☐ URequirementWatcherComponent added to APlayerState
☐ UPersistenceRegistrationComponent added to APlayerState with player persistence tag
☐ UQuestConfigDataAsset created and assigned to UQuestComponent::QuestConfig
☐ "QuestDefinition" primary asset type registered in DefaultGame.ini
☐ Gameplay tags from Architecture.md copied to DefaultGameplayTags.ini
☐ UQuestDefinition assets created in /Game/Quests/ (name == QuestId leaf)
☐ UQuestTrackerBridge (or equivalent) added to APlayerState
☐ GMS listeners added in reward/journal systems for GameCoreEvent.Quest.Completed
```

## Checklist: Shared Quest Extension

```
☐ USharedQuestComponent replaces UQuestComponent on APlayerState
☐ APlayerState implements IGroupProvider
☐ USharedQuestCoordinator added to group actor
☐ Group actor implements IGroupProvider::GetGroupActor() returning itself
☐ OnRequestGroupEnrollment delegate bound
☐ USharedQuestDefinition assets created for group quests
```
