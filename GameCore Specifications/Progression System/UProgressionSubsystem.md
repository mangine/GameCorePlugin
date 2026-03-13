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

- `ULevelingComponent` — resolved at runtime via `PS->GetPawn()->FindComponentByClass`. Never stored as a hard reference.
- `UAbilitySystemComponent` — queried for personal XP multiplier attribute. Soft dependency; skipped if ASC absent.
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
     * Grants XP to a player's progression track.
     * Resolves multipliers, delegates final XP to ULevelingComponent, dispatches audit.
     * Server-only — no-op on client.
     */
    void GrantXP(
        APlayerState* PS,
        FGameplayTag ProgressionTag,
        int32 BaseAmount,
        int32 ContentLevel,
        TScriptInterface<ISourceIDInterface> Source
    );

    void  SetGlobalXPMultiplier(float Multiplier);
    float GetGlobalXPMultiplier() const { return GlobalXPMultiplier; }

private:
    float ResolveMultiplier(APlayerState* PS) const;
    float GlobalXPMultiplier = 1.f;

    // Intra-system: bound to ULevelingComponent::OnLevelUp for audit only.
    void HandleLevelUpForAudit(FGameplayTag ProgressionTag, int32 NewLevel);
};
```

---

## GrantXP Flow

```
GrantXP(PS, Tag, BaseAmount, ContentLevel, Source)
  └── ResolveMultiplier (GlobalXPMultiplier × GAS personal multiplier)
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
    if (ULevelingComponent* LC = PS->GetPawn()->FindComponentByClass<ULevelingComponent>())
    {
        // Intra-system binding: audit only.
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

## Global XP Multiplier

```cpp
ProgressionSubsystem->SetGlobalXPMultiplier(2.f); // double-XP event
ProgressionSubsystem->SetGlobalXPMultiplier(1.f); // revert
```

Server-side policy only — not replicated. Clients informed via game-layer event messaging.

---

## Notes

- `GrantXP` is not `BlueprintCallable` — C++ server-side API only.
- The watcher system integrates via a thin adapter that listens to `GameCoreEvent.Progression.LevelUp` on GMS and calls `URequirementWatcherManager::NotifyPlayerEvent`. That adapter lives in the game layer or a dedicated integration module — never inside the Progression module.
