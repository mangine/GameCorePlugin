# ULevelingComponent

## Overview

`ULevelingComponent` is a replicated `UActorComponent` that manages one or more progression tracks on an Actor. Each track is identified by a `FGameplayTag` and has its own level, XP, and definition asset. The component is the single source of truth for progression state.

It fires **intra-system delegates** for consumers within the Progression module (e.g. `UProgressionSubsystem` for audit), and broadcasts to the **Event Bus** (`UGameCoreEventBus`) for all external integrations. External systems — watcher adapters, quest systems, UI — must listen via GMS and must not bind directly to these delegates.

All mutations are **server-only**. The client receives delta-replicated state via FastArray.

> **XP grants must go through `UProgressionSubsystem::GrantXP`.** `ApplyXP` is internal and not exposed to gameplay code or Blueprints.

## Plugin Module

`GameCore` (runtime module)

## File Location

```
GameCore/Source/GameCore/Progression/
└── LevelingComponent.h / .cpp
```

## Dependencies

- `UPointPoolComponent` — soft dependency; resolved at runtime via `GetOwner()->FindComponentByClass`.
- `URequirement` — for advanced prerequisite evaluation.
- `ISourceIDInterface` — for XP audit source identification.
- `ULevelProgressionDefinition` — data asset defining each progression.
- `UXPReductionPolicy` — sampled by `ApplyXP` to compute final XP from war XP.
- `IPersistableComponent` — implemented for binary save/load via the Serialization System.
- `UGameCoreEventBus` — broadcast target for cross-system events.

---

## Class Definition

```cpp
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API ULevelingComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()

public:
    ULevelingComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    bool RegisterProgression(ULevelProgressionDefinition* Definition);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    void UnregisterProgression(FGameplayTag ProgressionTag);

    // Internal — called only by UProgressionSubsystem.
    void ApplyXP(FGameplayTag ProgressionTag, int32 WarXP, int32 ContentLevel);
    int32 GetLastAppliedXPDelta() const { return LastAppliedXPDelta; }

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
    // Delegates — INTRA-SYSTEM ONLY
    // -------------------------------------------------------------------------

    // Fired on the server when a level-up or level-down occurs.
    // UProgressionSubsystem binds this for audit.
    // External systems MUST use GMS channel GameCoreEvent.Progression.LevelUp instead.
    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionLevelUp OnLevelUp;
    // Signature: (FGameplayTag ProgressionTag, int32 NewLevel)

    // Fired whenever XP changes (server-side).
    // External systems MUST use GMS channel GameCoreEvent.Progression.XPChanged instead.
    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionXPChanged OnXPChanged;
    // Signature: (FGameplayTag ProgressionTag, int32 NewXP, int32 Delta)

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
    LastAppliedXPDelta  = FinalXP;
    if (FinalXP == 0) return;

    // ... XP mutation ...

    // 1. Intra-system delegate (UProgressionSubsystem audit binding).
    OnXPChanged.Broadcast(ProgressionTag, NewXP, FinalXP);

    // 2. Event Bus — all external consumers listen here.
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FProgressionXPChangedMessage Msg;
        Msg.Instigator     = nullptr;           // Populated by UProgressionSubsystem before broadcast.
        Msg.Subject        = GetOwner();        // The Actor whose LevelingComponent was mutated.
        Msg.ProgressionTag = ProgressionTag;
        Msg.NewXP          = NewXP;
        Msg.Delta          = FinalXP;
        Bus->Broadcast(GameCoreEventTags::Progression_XPChanged, Msg,
            EGameCoreEventScope::ServerOnly);
    }
}
```

> **Note:** `Instigator` is set by `UProgressionSubsystem::GrantXP` on the message struct before `ApplyXP` is called, or alternatively the subsystem broadcasts the message itself post-`ApplyXP`. Either pattern keeps `ULevelingComponent` free of any `APlayerState` knowledge. Prefer the subsystem-broadcasts pattern to keep the component fully self-contained.

## ProcessLevelUp Logic

```cpp
void ULevelingComponent::ProcessLevelUp(FProgressionLevelData& Data, ULevelProgressionDefinition* Def)
{
    const int32 OldLevel = Data.Level;
    Data.Level++;
    GrantPointsForLevel(Def, Data.Level);

    // 1. Intra-system — UProgressionSubsystem binds for audit only.
    OnLevelUp.Broadcast(Data.ProgressionTag, Data.Level);

    // 2. Event Bus — quest system, achievement system, watcher adapter, UI all listen here.
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FProgressionLevelUpMessage Msg;
        Msg.Instigator     = nullptr;           // Populated by UProgressionSubsystem.
        Msg.Subject        = GetOwner();        // The Actor that leveled up.
        Msg.ProgressionTag = Data.ProgressionTag;
        Msg.OldLevel       = OldLevel;
        Msg.NewLevel       = Data.Level;
        Bus->Broadcast(GameCoreEventTags::Progression_LevelUp, Msg,
            EGameCoreEventScope::ServerOnly);
    }
}
```

---

## GMS Message Structs

See [GameCore Event Messages](../Event%20Bus%20System/GameCore%20Event%20Messages.md) for the canonical struct definitions. The structs are owned by the originating system but their canonical documentation lives in the Event Bus sub-pages.

---

## Replication Design

| Data | Replication Strategy | Notes |
| --- | --- | --- |
| `ProgressionData` | `FFastArraySerializer` | Delta-compressed per-element |
| `Definitions` | Not replicated | Server-side only |
| Level-up (external) | `GameCoreEvent.Progression.LevelUp` via Event Bus | Server broadcasts |
| Level-up (internal) | `OnLevelUp` delegate | `UProgressionSubsystem` only |
| XP change (external) | `GameCoreEvent.Progression.XPChanged` via Event Bus | Server broadcasts |
| XP change (internal) | `OnXPChanged` delegate | `UProgressionSubsystem` only |

---

## Server Authority Rules

- `ApplyXP`, `RegisterProgression`, `UnregisterProgression`, `DeserializeFromSave`, `DeserializeFromString` are server-only.
- No `SetLevel` / `SetXP` exposed — level is always a result of XP accumulation.
- All external XP grants go through `UProgressionSubsystem::GrantXP`.

---

## Negative XP Behavior

**`bAllowLevelDecrement = false` (default):**
- `NewXP = FMath::Max(CurrentXP + FinalXP, 0)` — XP floor is 0.
- Level never decreases.

**`bAllowLevelDecrement = true`:**
- XP reduces freely; level decrements on underflow, wraps to top of previous level.
- Level clamped at 1. XP at level 1 clamps at 0.
- `OnLevelUp` / `GameCoreEvent.Progression.LevelUp` fire with the new (lower) level.
