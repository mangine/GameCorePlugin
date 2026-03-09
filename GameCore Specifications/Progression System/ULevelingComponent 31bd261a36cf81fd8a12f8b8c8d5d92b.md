# ULevelingComponent

## Overview

`ULevelingComponent` is a replicated `UActorComponent` that manages one or more progression tracks on an Actor. Each track is identified by a `FGameplayTag` and has its own level, XP, and definition asset. The component is the single source of truth for progression state and fires delegates on level-up for other systems to react to.

All mutations are **server-only**. The client receives delta-replicated state via FastArray and reacts through delegates.

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

---

## Class Definition

```cpp
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API ULevelingComponent : public UActorComponent
{
    GENERATED_BODY()

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------
public:
    ULevelingComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /**
     * Registers a progression definition on this component.
     * Checks prerequisites before allowing registration.
     * Server-only.
     *
     * @param Definition   The DataAsset describing this progression.
     * @return True if registration succeeded (prerequisites met, not duplicate).
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    bool RegisterProgression(ULevelProgressionDefinition* Definition);

    /**
     * Removes a progression track entirely.
     * Does NOT refund granted points. Server-only.
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    void UnregisterProgression(FGameplayTag ProgressionTag);

    // -------------------------------------------------------------------------
    // XP
    // -------------------------------------------------------------------------

    /**
     * Adds XP to a progression. Negative values reduce XP but never below
     * the base XP floor of the current level (reputation safe).
     * Server-only.
     *
     * @param ProgressionTag  Which progression receives the XP.
     * @param Amount          XP delta (positive or negative).
     * @param Source          Optional source for audit/telemetry.
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    void AddXP(
        FGameplayTag ProgressionTag,
        int32 Amount,
        TScriptInterface<ISourceIDInterface> Source
    );

    // -------------------------------------------------------------------------
    // Queries (safe to call from client)
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Progression")
    int32 GetLevel(FGameplayTag ProgressionTag) const;

    UFUNCTION(BlueprintPure, Category = "Progression")
    int32 GetCurrentXP(FGameplayTag ProgressionTag) const;

    UFUNCTION(BlueprintPure, Category = "Progression")
    int32 GetXPRequiredForNextLevel(FGameplayTag ProgressionTag) const;

    UFUNCTION(BlueprintPure, Category = "Progression")
    bool IsProgressionRegistered(FGameplayTag ProgressionTag) const;

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence")
    FString SerializeToString() const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence")
    void DeserializeFromString(const FString& Data);

    // -------------------------------------------------------------------------
    // Delegates
    // -------------------------------------------------------------------------

    // Fired on server and broadcast to owning client when a level-up occurs.
    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionLevelUp OnLevelUp;
    // Signature: (FGameplayTag ProgressionTag, int32 NewLevel)

    // Fired whenever XP changes (server-side; clients react to replicated state).
    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionXPChanged OnXPChanged;
    // Signature: (FGameplayTag ProgressionTag, int32 NewXP, int32 Delta)

    // -------------------------------------------------------------------------
    // Private
    // -------------------------------------------------------------------------
private:
    UPROPERTY(Replicated)
    FProgressionLevelDataArray ProgressionData;

    // Tag -> Definition map; not replicated (client resolves via tag if needed)
    UPROPERTY()
    TMap<FGameplayTag, TObjectPtr<ULevelProgressionDefinition>> Definitions;

    FProgressionLevelData* FindProgressionData(FGameplayTag Tag);
    const FProgressionLevelData* FindProgressionData(FGameplayTag Tag) const;

    void ProcessLevelUp(FProgressionLevelData& Data, ULevelProgressionDefinition* Def);
    void GrantPointsForLevel(ULevelProgressionDefinition* Def, int32 NewLevel);
};
```

---

## Replication Design

| Data | Replication Strategy | Notes |
| --- | --- | --- |
| `ProgressionData` (levels + XP) | `FFastArraySerializer` | Delta-compressed per-element |
| `Definitions` map | Not replicated | Server-side only; client resolves from tag + loaded assets |
| Level-up notification | `OnLevelUp` delegate | Server fires; also triggers client-side via replicated state change |

FastArray ensures only changed progression entries are sent over the wire, which is critical at MMORPG scale where a character may have 20+ progressions.

---

## Server Authority Rules

- `AddXP`, `RegisterProgression`, `UnregisterProgression`, `DeserializeFromString` are all `BlueprintAuthorityOnly` — calling from client is a no-op.
- The component never exposes a direct `SetLevel` or `SetXP` — level is always a result of XP accumulation to prevent exploit surface.
- Prerequisite checks in `RegisterProgression` run on the server; the client cannot register a progression directly.

---

## Reputation / Negative XP Behavior

When `AddXP` is called with a negative value:

1. `NewXP = FMath::Max(CurrentXP + Delta, 0)` — XP floor is 0 (base of current level).
2. Level **never decreases** — only XP within the current level is affected.
3. `OnXPChanged` fires with the clamped delta.

This models reputation systems (GW2 WvW rank, ESO faction standing) where standing fluctuates but rank is permanent.

---

## Usage Example

```cpp
// Register a progression on BeginPlay (server)
if (HasAuthority())
{
    ULevelProgressionDefinition* Def = LoadObject<ULevelProgressionDefinition>(...);
    LevelingComp->RegisterProgression(Def);
}

// Grant XP from a mob kill
LevelingComp->AddXP(
    FGameplayTag::RequestGameplayTag("Progression.Swordsmanship"),
    50,
    MobActor  // implements ISourceIDInterface
);

// Query (safe from client)
int32 Level = LevelingComp->GetLevel(
    FGameplayTag::RequestGameplayTag("Progression.Swordsmanship")
);
```