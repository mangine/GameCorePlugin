# Progression System — Usage

All XP grants flow through `UProgressionSubsystem`. Components are never called directly by gameplay code.

---

## Setup — Actor Configuration

### 1. Add Components to the Actor

```cpp
// In your ACharacter or APawn class (server-side actor)
ULevelingComponent*   LevelingComp   = CreateDefaultSubobject<ULevelingComponent>(TEXT("LevelingComp"));
UPointPoolComponent*  PointPoolComp  = CreateDefaultSubobject<UPointPoolComponent>(TEXT("PointPoolComp"));

// Also add UPersistenceRegistrationComponent so the Serialization System
// auto-discovers and saves both components via IPersistableComponent.
UPersistenceRegistrationComponent* PersistComp =
    CreateDefaultSubobject<UPersistenceRegistrationComponent>(TEXT("PersistComp"));
PersistComp->PersistenceTag = TAG_Persistence_Entity_Player;
```

### 2. Register Progressions (Server BeginPlay)

```cpp
void AMyCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority()) return;

    // Register each progression with its definition asset.
    // Prerequisites are checked here — registration silently fails if unmet.
    LevelingComp->RegisterProgression(CharacterLevelDefinition);
    LevelingComp->RegisterProgression(SwordsmanshipDefinition);
    LevelingComp->RegisterProgression(NavigationDefinition);

    // Register point pools (must match PoolTag values in grant definitions).
    PointPoolComp->RegisterPool(TAG_Points_Skill,     /* Cap */ 0);
    PointPoolComp->RegisterPool(TAG_Points_Attribute, /* Cap */ 50);
}
```

---

## Granting XP

### Standard Grant (Player-Driven)

The most common path: player kills an enemy, completes a task, etc.

```cpp
// Get the subsystem from the world.
UProgressionSubsystem* Progression = GetWorld()->GetSubsystem<UProgressionSubsystem>();

// Target defaults to Instigator->GetPawn() when nullptr.
Progression->GrantXP(
    PlayerState,               // Instigator — always required for multipliers & audit
    nullptr,                   // Target — nullptr = Instigator->GetPawn()
    TAG_Progression_Character_Level,
    200,                       // BaseAmount
    EnemyLevel,                // ContentLevel — used by XPReductionPolicy
    EnemySourceID              // ISourceIDInterface implementation on the enemy
);
```

### Grant to a Different Target (Crew Member, NPC)

```cpp
// Grant XP to a crew member Actor while keeping multipliers from the player.
Progression->GrantXP(
    PlayerState,               // Instigator — drives multipliers & audit attribution
    CrewMemberActor,           // Target — the Actor that owns ULevelingComponent
    TAG_Progression_CrewMember_Navigation,
    150,
    ContentLevel,
    QuestSourceID
);
```

### Double-XP Event

```cpp
// Set global multiplier (server-only, not replicated — inform clients via your own event).
Progression->SetGlobalXPMultiplier(2.f);

// Revert after event ends.
Progression->SetGlobalXPMultiplier(1.f);
```

---

## Granting Points Directly (Non-Leveling Source)

```cpp
// Quest reward, achievement, GM grant — directly on the component.
UPointPoolComponent* PoolComp = Actor->FindComponentByClass<UPointPoolComponent>();
if (PoolComp)
{
    EPointAddResult Result = PoolComp->AddPoints(TAG_Points_Skill, 3);
    if (Result == EPointAddResult::PartialCap)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("Pool %s is capped — some points were lost. Check cap configuration."),
            *TAG_Points_Skill.ToString());
    }
}
```

## Spending Points

```cpp
UPointPoolComponent* PoolComp = Actor->FindComponentByClass<UPointPoolComponent>();
if (PoolComp && PoolComp->ConsumePoints(TAG_Points_Skill, 1))
{
    // Points successfully spent — apply the purchased upgrade here.
}
```

## Querying State

```cpp
ULevelingComponent* LevelComp = Actor->FindComponentByClass<ULevelingComponent>();

int32 Level          = LevelComp->GetLevel(TAG_Progression_Swordsmanship);  // e.g. 15
int32 CurrentXP      = LevelComp->GetXP(TAG_Progression_Swordsmanship);     // e.g. 450
int32 XPToNextLevel  = LevelComp->GetXPToNextLevel(TAG_Progression_Swordsmanship); // e.g. 1200
bool  bIsRegistered  = LevelComp->IsProgressionRegistered(TAG_Progression_Swordsmanship);

UPointPoolComponent* PoolComp = Actor->FindComponentByClass<UPointPoolComponent>();
int32 Spendable      = PoolComp->GetSpendable(TAG_Points_Skill);   // Available - Consumed
int32 Consumed       = PoolComp->GetConsumed(TAG_Points_Skill);
```

---

## Reacting to Level-Up Events

Do **not** bind to `OnLevelUp` delegate directly from external systems. Listen on the Event Bus instead.

```cpp
// In your quest system, achievement system, or watcher adapter:
// Bind on the server to the level-up GMS channel.
if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
{
    Bus->Subscribe<FProgressionLevelUpMessage>(
        GameCoreEventTags::Progression_LevelUp,
        this,
        &UMyQuestSystem::OnProgressionLevelUp
    );
}

void UMyQuestSystem::OnProgressionLevelUp(const FProgressionLevelUpMessage& Msg)
{
    // Msg.Subject        — the Actor that leveled up
    // Msg.ProgressionTag — which progression (TAG_Progression_Character_Level, etc.)
    // Msg.OldLevel       — previous level
    // Msg.NewLevel       — new level
    // Msg.Instigator     — the APlayerState that triggered the grant (may be nullptr for non-player grants)

    // Example: advance a quest objective
    if (Msg.ProgressionTag == TAG_Progression_Swordsmanship && Msg.NewLevel >= 10)
    {
        AdvanceQuestObjective(Msg.Subject, TAG_Quest_Objective_MasterSword);
    }
}
```

### Reacting to XP Changes

```cpp
Bus->Subscribe<FProgressionXPChangedMessage>(
    GameCoreEventTags::Progression_XPChanged,
    this,
    &UMyUIManager::OnXPChanged
);
```

### Reacting to Pool Changes

```cpp
Bus->Subscribe<FProgressionPointPoolChangedMessage>(
    GameCoreEventTags::Progression_PointPoolChanged,
    this,
    &UMyUIManager::OnPointPoolChanged
);
```

---

## Implementing a Custom XP Source

Any UObject that triggers XP grants must implement `ISourceIDInterface` (from `GameCore Core`).

```cpp
// In your enemy character header:
class AMyEnemy : public ACharacter, public ISourceIDInterface
{
    GENERATED_BODY()
public:
    virtual FGameplayTag GetSourceTag() const override
    {
        return TAG_Source_Mob_Skeleton_Level10;  // defined in your project's tag ini
    }
    virtual FText GetSourceDisplayName() const override
    {
        return NSLOCTEXT("Game", "SkeletonL10", "Skeleton (Level 10)");
    }
};
```

Pass it as the `Source` argument to `GrantXP`.

---

## Implementing a Custom XP Reduction Policy

```cpp
// Stepped brackets instead of a smooth curve (e.g. WoW-style).
UCLASS(BlueprintType, EditInlineNew)
class UXPReductionPolicyBracket : public UXPReductionPolicy
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = "Reduction")
    TArray<FLevelBracketEntry> Brackets;

    virtual float Evaluate(int32 PlayerLevel, int32 ContentLevel) const override
    {
        const int32 Gap = PlayerLevel - ContentLevel;
        for (const FLevelBracketEntry& B : Brackets)
        {
            if (Gap >= B.MinGap && Gap <= B.MaxGap)
                return B.Multiplier;
        }
        return 0.f;  // trivial content
    }
};
```

Assign the policy inline in the `ULevelProgressionDefinition` data asset editor — no code changes to the subsystem or component required.

---

## Creating a Progression Definition (Data Asset)

Create a `ULevelProgressionDefinition` data asset in the editor:

```
Content Browser → Add → Miscellaneous → Data Asset → ULevelProgressionDefinition
Name: DA_Progression_Swordsmanship
```

Configure fields:

| Field | Example Value |
|---|---|
| `ProgressionTag` | `Progression.Character.Swordsmanship` |
| `MaxLevel` | `60` |
| `bAllowLevelDecrement` | `false` |
| `XPCurveType` | `Formula` |
| `FormulaParams.Base` | `100` |
| `FormulaParams.Exponent` | `1.5` |
| `ReductionPolicy` | `UXPReductionPolicyCurve` (inline instance, assign `ReductionCurve` asset) |
| `LevelUpGrant.PoolTag` | `Points.Skill` |
| `LevelUpGrant.CurveType` | `Constant` |
| `LevelUpGrant.ConstantAmount` | `1` |

---

## Persistence

Both `ULevelingComponent` and `UPointPoolComponent` implement `IPersistableComponent`. The Serialization System calls `SerializeForSave` / `DeserializeFromSave` automatically when `UPersistenceRegistrationComponent` is on the same actor.

No manual save calls are needed. For GM debugging:

```cpp
// Server-only debug helpers — never called on the save path.
FString Snapshot = LevelingComp->SerializeToString();
LevelingComp->DeserializeFromString(Snapshot);
```
