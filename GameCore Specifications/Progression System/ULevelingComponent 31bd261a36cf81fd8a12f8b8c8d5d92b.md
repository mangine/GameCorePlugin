# ULevelingComponent

## Overview

`ULevelingComponent` is a replicated `UActorComponent` that manages one or more progression tracks on an Actor. Each track is identified by a `FGameplayTag` and has its own level, XP, and definition asset. The component is the single source of truth for progression state and fires delegates on level-up for intra-system consumers, and broadcasts to `UGameCoreEventSubsystem` for all external integrations.

All mutations are **server-only**. The client receives delta-replicated state via FastArray and reacts through delegates.

> **XP grants must go through `UProgressionSubsystem::GrantXP`.** `ApplyXP` is internal and not exposed to gameplay code or Blueprints. Direct calls bypass multiplier resolution, audit, and watcher notification.

## Plugin Module

`GameCore` (runtime module)

## File Location

```
GameCore/Source/GameCore/Progression/
└── LevelingComponent.h / .cpp
```

## Dependencies

- `UPointPoolComponent` — soft dependency; resolved at runtime via `GetOwner()->FindComponentByClass`. Not required for the component to function without point grants.
- `URequirement` — for advanced prerequisite evaluation.
- `ISourceIDInterface` — for XP audit source identification.
- `ULevelProgressionDefinition` — data asset defining each progression.
- `UXPReductionPolicy` — sampled by `ApplyXP` to compute final XP from war XP.
- `IPersistableComponent` — implemented for binary save/load via the Serialization System.
- `UGameCoreEventSubsystem` — broadcast target for cross-system events.

---

## Class Definition

```cpp
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API ULevelingComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------
public:
    ULevelingComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    bool RegisterProgression(ULevelProgressionDefinition* Definition);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    void UnregisterProgression(FGameplayTag ProgressionTag);

    // -------------------------------------------------------------------------
    // XP  (internal — called only by UProgressionSubsystem)
    // -------------------------------------------------------------------------

    void ApplyXP(FGameplayTag ProgressionTag, int32 WarXP, int32 ContentLevel);
    int32 GetLastAppliedXPDelta() const { return LastAppliedXPDelta; }

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetLevel(FGameplayTag ProgressionTag) const;

    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetXP(FGameplayTag ProgressionTag) const;

    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetXPToNextLevel(FGameplayTag ProgressionTag) const;

    UFUNCTION(BlueprintCallable, Category = "Progression")
    bool IsProgressionRegistered(FGameplayTag ProgressionTag) const;

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    virtual void SerializeForSave(FArchive& Ar) override;
    virtual void DeserializeFromSave(FArchive& Ar) override;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    FString SerializeToString() const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    void DeserializeFromString(const FString& Data);

    // -------------------------------------------------------------------------
    // Delegates  (intra-system only)
    // -------------------------------------------------------------------------

    // Fired on the server when a level-up or level-down occurs.
    // UProgressionSubsystem subscribes to this for audit and watcher notification.
    // External systems must use the GMS channel GameCoreEvent.Progression.LevelUp instead.
    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionLevelUp OnLevelUp;
    // Signature: (FGameplayTag ProgressionTag, int32 NewLevel)

    // Fired whenever XP changes (server-side; clients react to replicated state).
    // External systems must use the GMS channel GameCoreEvent.Progression.XPChanged instead.
    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionXPChanged OnXPChanged;
    // Signature: (FGameplayTag ProgressionTag, int32 NewXP, int32 Delta)

    // -------------------------------------------------------------------------
    // Private
    // -------------------------------------------------------------------------
private:
    UPROPERTY(Replicated)
    FProgressionLevelDataArray ProgressionData;

    UPROPERTY()
    TMap<FGameplayTag, TObjectPtr<ULevelProgressionDefinition>> Definitions;

    int32 LastAppliedXPDelta = 0;

    FProgressionLevelData* FindProgressionData(FGameplayTag Tag);
    const FProgressionLevelData* FindProgressionData(FGameplayTag Tag) const;

    void ProcessLevelUp(FProgressionLevelData& Data, ULevelProgressionDefinition* Def);
    void ProcessLevelDown(FProgressionLevelData& Data, ULevelProgressionDefinition* Def);
    void GrantPointsForLevel(ULevelProgressionDefinition* Def, int32 NewLevel);
};
```

---

## ApplyXP Logic

```cpp
void ULevelingComponent::ApplyXP(FGameplayTag ProgressionTag, int32 WarXP, int32 ContentLevel)
{
    LastAppliedXPDelta = 0;

    ULevelProgressionDefinition* Def = Definitions.FindRef(ProgressionTag);
    if (!Def) return;

    const float Reduction = Def->ReductionPolicy
        ? Def->ReductionPolicy->Evaluate(GetLevel(ProgressionTag), ContentLevel)
        : 1.f;

    const int32 FinalXP = FMath::RoundToInt(WarXP * Reduction);
    LastAppliedXPDelta = FinalXP;

    if (FinalXP == 0) return;

    // ... XP mutation and level-up processing ...

    // 1. Fire intra-system delegate (UProgressionSubsystem binds this for audit/watcher).
    OnXPChanged.Broadcast(ProgressionTag, NewXP, FinalXP);

    // 2. Broadcast to GMS for all external consumers.
    if (UGameCoreEventSubsystem* EventBus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
    {
        FProgressionXPChangedMessage Msg;
        Msg.PlayerState    = GetOwner<APlayerState>();
        Msg.ProgressionTag = ProgressionTag;
        Msg.NewXP          = NewXP;
        Msg.Delta          = FinalXP;
        EventBus->Broadcast(GameCoreEventTags::Progression_XPChanged, Msg);
    }
}
```

## ProcessLevelUp Logic

```cpp
void ULevelingComponent::ProcessLevelUp(FProgressionLevelData& Data, ULevelProgressionDefinition* Def)
{
    const int32 OldLevel = Data.Level;
    Data.Level++;

    GrantPointsForLevel(Def, Data.Level);

    // 1. Intra-system delegate — UProgressionSubsystem binds this for audit and watcher notification.
    OnLevelUp.Broadcast(Data.ProgressionTag, Data.Level);

    // 2. GMS broadcast — all external systems listen here (quest, achievement, watcher, UI).
    if (UGameCoreEventSubsystem* EventBus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
    {
        FProgressionLevelUpMessage Msg;
        Msg.PlayerState    = GetOwner<APlayerState>();
        Msg.ProgressionTag = Data.ProgressionTag;
        Msg.OldLevel       = OldLevel;
        Msg.NewLevel       = Data.Level;
        EventBus->Broadcast(GameCoreEventTags::Progression_LevelUp, Msg);
    }
}
```

---

## Replication Design

| Data | Replication Strategy | Notes |
| --- | --- | --- |
| `ProgressionData` (levels + XP) | `FFastArraySerializer` | Delta-compressed per-element |
| `Definitions` map | Not replicated | Server-side only |
| Level-up notification (external) | `GameCoreEvent.Progression.LevelUp` via GMS | Server broadcasts; external systems listen |
| Level-up notification (internal) | `OnLevelUp` delegate | `UProgressionSubsystem` only |

---

## Server Authority Rules

- `ApplyXP`, `RegisterProgression`, `UnregisterProgression`, `DeserializeFromSave`, and `DeserializeFromString` are all server-only.
- The component never exposes a direct `SetLevel` or `SetXP`.
- All external XP grants go through `UProgressionSubsystem::GrantXP`.

---

## Negative XP Behavior

**`bAllowLevelDecrement = false` (default):**
1. `NewXP = FMath::Max(CurrentXP + FinalXP, 0)` — XP floor is 0.
2. Level never decreases.
3. `OnXPChanged` / `GameCoreEvent.Progression.XPChanged` fire with the clamped delta.

**`bAllowLevelDecrement = true`:**
1. XP reduces freely. If it drops below 0, level decrements and XP wraps to the top of the previous level.
2. Level clamped at 1. XP at level 1 clamps at 0.
3. `OnLevelUp` / `GameCoreEvent.Progression.LevelUp` fire with the new (lower) level.
