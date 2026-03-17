# UProgressionSubsystem

**Sub-page of:** [Progression System](../Progression%20System%2031bd261a36cf8139a371f3c7327ae7d8.md)

## Overview

`UProgressionSubsystem` is a **server-only** `UWorldSubsystem` that acts as the sole external entry point for all XP grants. It owns the policy layer — multiplier resolution and audit dispatch — while components remain pure state owners.

The watcher system is **not** a dependency of the Progression System. Any system that needs to react to level-up events for requirement evaluation must listen to `GameCoreEvent.Progression.LevelUp` on GMS and call `URequirementWatcherManager::NotifyPlayerEvent` from its own adapter. This keeps the Progression module fully independent.

## Plugin Module

`GameCore` (runtime module)

## File Location

```
GameCore/Source/GameCore/Progression/
└── ProgressionSubsystem.h / .cpp
```

## Dependencies

- `ULevelingComponent` — resolved at runtime from `Target` via `FindComponentByClass`. Never stored as a hard reference.
- `UAbilitySystemComponent` — queried on `Instigator` for personal XP multiplier attribute. Soft dependency; skipped if ASC absent.
- `FGameCoreBackend` — audit dispatch via `FGameCoreBackend::GetAudit(TAG_Audit_Progression)`.

> **Removed dependency:** `URequirementWatcherManager` is no longer a direct dependency. Watcher notification is decoupled via the `GameCoreEvent.Progression.LevelUp` GMS channel.

---

## Class Definition

```cpp
UCLASS()
class GAMECORE_API UProgressionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * Grants XP to any Actor that owns a ULevelingComponent.
     *
     * @param Instigator  The player driving this grant. Always required — used for
     *                    multiplier resolution (GlobalXP × ASC personal multiplier)
     *                    and audit attribution.
     * @param Target      The Actor whose ULevelingComponent receives the XP.
     *                    Pass nullptr to default to Instigator->GetPawn().
     *                    Can be an NPC, ship crew member, or any Actor with a
     *                    ULevelingComponent.
     * @param ProgressionTag  The progression track to credit.
     * @param BaseAmount      Raw XP before multipliers.
     * @param ContentLevel    Used by UXPReductionPolicy to scale XP for level-gap.
     * @param Source          Audit source identifier.
     *
     * Server-only — no-op on client.
     */
    void GrantXP(
        APlayerState* Instigator,
        AActor*        Target,
        FGameplayTag   ProgressionTag,
        int32          BaseAmount,
        int32          ContentLevel,
        TScriptInterface<ISourceIDInterface> Source
    );

    /** Convenience overload — Target defaults to Instigator->GetPawn(). */
    void GrantXP(
        APlayerState* Instigator,
        FGameplayTag  ProgressionTag,
        int32         BaseAmount,
        int32         ContentLevel,
        TScriptInterface<ISourceIDInterface> Source
    );

    void  SetGlobalXPMultiplier(float Multiplier);
    float GetGlobalXPMultiplier() const { return GlobalXPMultiplier; }

private:
    /**
     * Resolves the combined XP multiplier for the Instigator.
     * GlobalXPMultiplier × personal GAS attribute multiplier (if ASC present).
     * Always keyed to the player — NPCs and other target Actors do not contribute multipliers.
     */
    float ResolveMultiplier(APlayerState* Instigator) const;
    float GlobalXPMultiplier = 1.f;

    // Intra-system: bound to ULevelingComponent::OnLevelUp for audit only.
    void HandleLevelUpForAudit(FGameplayTag ProgressionTag, int32 NewLevel);
};
```

---

## GrantXP Flow

```
GrantXP(Instigator, Target, Tag, BaseAmount, ContentLevel, Source)
  └── Target = (Target != nullptr) ? Target : Instigator->GetPawn()
  └── LevelingComp = Target->FindComponentByClass<ULevelingComponent>()
  └── if (!LevelingComp) return   // silent no-op — Target has no leveling
  └── ResolveMultiplier(Instigator)   ← always player-driven
        GlobalXPMultiplier × GAS personal multiplier on Instigator's ASC
  └── LevelingComp->ApplyXP(Tag, WarXP, ContentLevel)
        ├── Applies UXPReductionPolicy
        ├── Mutates ProgressionData
        ├── Fires OnXPChanged delegate  (intra-system)
        ├── Broadcasts GameCoreEvent.Progression.XPChanged via GMS
        └── On level-up:
              ├── Fires OnLevelUp delegate  (intra-system → subsystem audit)
              └── Broadcasts GameCoreEvent.Progression.LevelUp via GMS
                    └── External listeners (no Progression module involvement):
                          ├── Watcher adapter → RequirementEvent.Leveling.LevelChanged
                          ├── Quest system
                          ├── Achievement system
                          └── UI / VFX
  └── Audit via FGameCoreBackend::GetAudit(TAG_Audit_Progression)
```

---

## Initialize

```cpp
void UProgressionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // No watcher dependency. Watcher integration is external via GMS.
}

void UProgressionSubsystem::OnPlayerRegistered(APlayerState* PS)
{
    // Bind audit to the player's own pawn's LevelingComponent.
    // For NPCs / other targets, audit fires via the anonymous HandleLevelUpForAudit
    // delegate bound at GrantXP time (not needed — see Note below).
    if (ULevelingComponent* LC = PS->GetPawn()->FindComponentByClass<ULevelingComponent>())
    {
        LC->OnLevelUp.AddUObject(this, &UProgressionSubsystem::HandleLevelUpForAudit);
    }
}

void UProgressionSubsystem::HandleLevelUpForAudit(FGameplayTag ProgressionTag, int32 NewLevel)
{
    FGameCoreBackend::GetAudit(TAG_Audit_Progression_LevelUp).Record(/* payload */);
    // Do NOT notify WatcherManager here — that coupling is intentionally removed.
}
```

---

## Convenience Overload Implementation

```cpp
void UProgressionSubsystem::GrantXP(
    APlayerState* Instigator,
    FGameplayTag  ProgressionTag,
    int32         BaseAmount,
    int32         ContentLevel,
    TScriptInterface<ISourceIDInterface> Source)
{
    GrantXP(Instigator, Instigator ? Instigator->GetPawn() : nullptr,
            ProgressionTag, BaseAmount, ContentLevel, Source);
}
```

Existing call sites that pass only a `APlayerState*` continue to compile unchanged.

---

## Global XP Multiplier

```cpp
ProgressionSubsystem->SetGlobalXPMultiplier(2.f); // double-XP event
ProgressionSubsystem->SetGlobalXPMultiplier(1.f); // revert
```

Server-side policy only — not replicated. Clients informed via game-layer event messaging.

---

## Notes

- `GrantXP` is not `BlueprintCallable` — C++ server-side API only.
- Multipliers are **always resolved from the Instigator** (the player). Target Actors such as NPCs or crew members never contribute their own multipliers — the player's progression bonuses are what scale the reward.
- The watcher system integrates via a thin adapter that listens to `GameCoreEvent.Progression.LevelUp` on GMS and calls `URequirementWatcherManager::NotifyPlayerEvent`. That adapter lives in the game layer or a dedicated integration module — never inside the Progression module.
- Call sites are responsible for resolving the correct `Target` Actor. The Progression System does not perform any routing — it applies XP to whatever `ULevelingComponent` it finds on the Target.
