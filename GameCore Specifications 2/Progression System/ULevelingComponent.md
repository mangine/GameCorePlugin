# ULevelingComponent

**File:** `GameCore/Source/GameCore/Progression/LevelingComponent.h/.cpp`

## Overview

`ULevelingComponent` is a replicated `UActorComponent` that manages one or more progression tracks on an Actor. Each track is identified by a `FGameplayTag` and has its own level, XP, and definition asset.

It fires **intra-system delegates** for consumers within the Progression module (e.g. `UProgressionSubsystem` for audit) and broadcasts to the **Event Bus** for all external integrations. External systems — quest system, achievement system, watcher adapters, UI — must listen via the Event Bus.

> **XP grants must always go through `UProgressionSubsystem::GrantXP`.** `ApplyXP` is internal and not accessible to gameplay code or Blueprints.

All mutations are **server-only**. Clients receive delta-replicated state via FastArray.

---

## Dependencies
- `UPointPoolComponent` — soft dependency; resolved at runtime via `FindComponentByClass`
- `ULevelProgressionDefinition` — data asset per progression, server-side only
- `UXPReductionPolicy` — sampled inside `ApplyXP`
- `IPersistableComponent` — implemented for binary save/load
- `UGameCoreEventBus` — Event Bus broadcast target
- `ISourceIDInterface` — not held by the component; passed through from the subsystem

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

    // -------------------------------------------------------------------------
    // Registration
    // -------------------------------------------------------------------------

    // Registers a progression on this component. Checks prerequisites before registering.
    // Silent no-op if prerequisites are not met or progression is already registered.
    // Server-only.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    bool RegisterProgression(ULevelProgressionDefinition* Definition);

    // Removes a progression track. Any accumulated XP and level are discarded.
    // Server-only.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    void UnregisterProgression(FGameplayTag ProgressionTag);

    // -------------------------------------------------------------------------
    // Internal XP Interface (UProgressionSubsystem only)
    // -------------------------------------------------------------------------

    // Applies war XP to a progression, sampling the reduction policy for final XP.
    // Not exposed to gameplay code or Blueprints. Server-only.
    void ApplyXP(FGameplayTag ProgressionTag, int32 WarXP, int32 ContentLevel);

    // Returns the final XP amount applied by the most recent ApplyXP call.
    // Read by UProgressionSubsystem for audit payload construction.
    int32 GetLastAppliedXPDelta() const { return LastAppliedXPDelta; }

    // -------------------------------------------------------------------------
    // Read API (safe on client)
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
    // Persistence — IPersistableComponent
    // -------------------------------------------------------------------------

    virtual void SerializeForSave(FArchive& Ar) override;
    virtual void DeserializeFromSave(FArchive& Ar) override;

    // Debug/tooling helpers — never called on the save path.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    FString SerializeToString() const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    void DeserializeFromString(const FString& Data);

    // -------------------------------------------------------------------------
    // Delegates — INTRA-SYSTEM ONLY
    // External systems must use the Event Bus (GameCoreEvent.Progression.*)
    // -------------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionLevelUp OnLevelUp;
    // Signature: (FGameplayTag ProgressionTag, int32 NewLevel)

    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionXPChanged OnXPChanged;
    // Signature: (FGameplayTag ProgressionTag, int32 NewXP, int32 Delta)

private:
    UPROPERTY(Replicated)
    FProgressionLevelDataArray ProgressionData;

    // Server-only: definition assets keyed by tag. Not replicated.
    UPROPERTY()
    TMap<FGameplayTag, TObjectPtr<ULevelProgressionDefinition>> Definitions;

    // Cached result of the most recent ApplyXP call. Read by UProgressionSubsystem.
    int32 LastAppliedXPDelta = 0;

    FProgressionLevelData* FindProgressionData(FGameplayTag Tag);
    const FProgressionLevelData* FindProgressionData(FGameplayTag Tag) const;

    void ProcessLevelUp(FProgressionLevelData& Data, ULevelProgressionDefinition* Def);
    void ProcessLevelDown(FProgressionLevelData& Data, ULevelProgressionDefinition* Def);
    void GrantPointsForLevel(ULevelProgressionDefinition* Def, int32 NewLevel);
};
```

---

## ApplyXP

`ApplyXP` is the only method that mutates progression state. It is called exclusively by `UProgressionSubsystem::GrantXP`.

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

    FProgressionLevelData* Data = FindProgressionData(ProgressionTag);
    if (!Data) return;

    const int32 OldXP   = Data->CurrentXP;
    const int32 NewXP   = Def->bAllowLevelDecrement
        ? Data->CurrentXP + FinalXP                         // Signed — can go negative
        : FMath::Max(Data->CurrentXP + FinalXP, 0);         // Floor at 0
    Data->CurrentXP = NewXP;
    ProgressionData.MarkItemDirty(*Data);

    // Intra-system delegate (subsystem reads LastAppliedXPDelta post-call).
    OnXPChanged.Broadcast(ProgressionTag, NewXP, FinalXP);

    // Check level-up: loop in case multiple levels gained in one grant.
    while (Data->CurrentLevel < Def->MaxLevel)
    {
        const int32 XPNeeded = Def->GetXPRequiredForLevel(Data->CurrentLevel);
        if (XPNeeded <= 0 || Data->CurrentXP < XPNeeded) break;
        Data->CurrentXP -= XPNeeded;
        ProcessLevelUp(*Data, Def);
    }

    // Check level-down (opt-in via bAllowLevelDecrement).
    if (Def->bAllowLevelDecrement)
    {
        while (Data->CurrentLevel > 1 && Data->CurrentXP < 0)
        {
            const int32 XPOfPrevLevel = Def->GetXPRequiredForLevel(Data->CurrentLevel - 1);
            Data->CurrentXP += XPOfPrevLevel;
            ProcessLevelDown(*Data, Def);
        }
        // Final floor: level 1, XP 0.
        if (Data->CurrentLevel == 1)
            Data->CurrentXP = FMath::Max(Data->CurrentXP, 0);
    }
}
```

---

## ProcessLevelUp

The `Instigator` field on the Event Bus message is left null here — `UProgressionSubsystem` broadcasts the full message post-`ApplyXP` with the correct `Instigator` populated. The component's internal delegate is for intra-system use only.

```cpp
void ULevelingComponent::ProcessLevelUp(FProgressionLevelData& Data, ULevelProgressionDefinition* Def)
{
    const int32 OldLevel = Data.CurrentLevel;
    Data.CurrentLevel++;
    ProgressionData.MarkItemDirty(Data);

    GrantPointsForLevel(Def, Data.CurrentLevel);

    // 1. Intra-system — UProgressionSubsystem binds for audit.
    OnLevelUp.Broadcast(Data.ProgressionTag, Data.CurrentLevel);
}
```

---

## ProcessLevelDown

```cpp
void ULevelingComponent::ProcessLevelDown(FProgressionLevelData& Data, ULevelProgressionDefinition* Def)
{
    Data.CurrentLevel--;
    ProgressionData.MarkItemDirty(Data);

    // Reuse OnLevelUp — NewLevel is lower, listeners inspect Msg.NewLevel vs Msg.OldLevel.
    OnLevelUp.Broadcast(Data.ProgressionTag, Data.CurrentLevel);
}
```

---

## GrantPointsForLevel

```cpp
void ULevelingComponent::GrantPointsForLevel(ULevelProgressionDefinition* Def, int32 NewLevel)
{
    if (!Def->LevelUpGrant.PoolTag.IsValid()) return;

    const int32 Amount = Def->GetGrantAmountForLevel(NewLevel);
    if (Amount <= 0) return;

    UPointPoolComponent* PoolComp = GetOwner()->FindComponentByClass<UPointPoolComponent>();
    if (!PoolComp)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("ULevelingComponent: %s leveled up but owner has no UPointPoolComponent — pool grant skipped."),
            *GetOwner()->GetName());
        return;
    }

    const EPointAddResult Result = PoolComp->AddPoints(Def->LevelUpGrant.PoolTag, Amount);
    if (Result == EPointAddResult::PartialCap)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("ULevelingComponent: Pool %s cap hit during level-up grant for %s. Points partially lost."),
            *Def->LevelUpGrant.PoolTag.ToString(), *GetOwner()->GetName());
    }
}
```

---

## Negative XP Behavior

| `bAllowLevelDecrement` | Behavior |
|---|---|
| `false` (default) | XP floors at 0. Level never decreases. ESO/GW2 reputation model. |
| `true` | XP is signed; level decrements on underflow, wraps to top of previous level. Level clamped at 1, XP clamped at 0 at level 1. |

---

## Replication Design

| Data | Strategy | Notes |
|---|---|---|
| `ProgressionData` | `FFastArraySerializer` | Delta-compressed per element |
| `Definitions` | Not replicated | Server-side only |
| Level-up (external) | `GameCoreEvent.Progression.LevelUp` via Event Bus | Broadcast by subsystem with full Instigator info |
| Level-up (internal) | `OnLevelUp` delegate | `UProgressionSubsystem` audit only |
| XP change (external) | `GameCoreEvent.Progression.XPChanged` via Event Bus | Broadcast by subsystem |
| XP change (internal) | `OnXPChanged` delegate | `UProgressionSubsystem` audit only |

---

## Server Authority Rules

- `ApplyXP`, `RegisterProgression`, `UnregisterProgression`, `DeserializeFromSave`, `DeserializeFromString` — server-only.
- No `SetLevel` / `SetXP` is exposed. Level is always a consequence of XP accumulation.
- All external XP grants must go through `UProgressionSubsystem::GrantXP`.
