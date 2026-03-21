# UProgressionSubsystem

**File:** `GameCore/Source/GameCore/Progression/ProgressionSubsystem.h/.cpp`

## Overview

`UProgressionSubsystem` is a **server-only** `UWorldSubsystem` and the **sole external entry point** for all XP grants. It owns the policy layer — multiplier resolution and audit dispatch — while components remain pure state owners.

Watcher system integration is **not a compile-time dependency** of this module. Any system that needs to react to level-up events must listen to `GameCoreEvent.Progression.LevelUp` on the Event Bus from its own adapter.

---

## Dependencies
- `ULevelingComponent` — resolved at runtime from `Target` via `FindComponentByClass`. Never stored as a hard reference.
- `UAbilitySystemComponent` — queried on `Instigator` for personal XP multiplier attribute. Soft dependency — skipped if ASC absent.
- `FGameCoreBackend` — audit dispatch via `FGameCoreBackend::GetAudit(TAG_Audit_Progression)`.

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
     * @param Instigator      The player driving this grant. Always required —
     *                        used for multiplier resolution and audit attribution.
     * @param Target          The Actor whose ULevelingComponent receives the XP.
     *                        Pass nullptr to default to Instigator->GetPawn().
     *                        Can be any Actor with a ULevelingComponent (NPC, crew, etc.)
     * @param ProgressionTag  The progression track to credit.
     * @param BaseAmount      Raw XP before multipliers and reduction.
     * @param ContentLevel    Passed to UXPReductionPolicy. Pass INDEX_NONE to skip reduction.
     * @param Source          Audit source identifier (ISourceIDInterface).
     *
     * Server-only — silent no-op on client.
     */
    void GrantXP(
        APlayerState*                         Instigator,
        AActor*                               Target,
        FGameplayTag                          ProgressionTag,
        int32                                 BaseAmount,
        int32                                 ContentLevel,
        TScriptInterface<ISourceIDInterface>  Source
    );

    /** Convenience overload — Target defaults to Instigator->GetPawn(). */
    void GrantXP(
        APlayerState*                         Instigator,
        FGameplayTag                          ProgressionTag,
        int32                                 BaseAmount,
        int32                                 ContentLevel,
        TScriptInterface<ISourceIDInterface>  Source
    );

    void  SetGlobalXPMultiplier(float Multiplier);
    float GetGlobalXPMultiplier() const { return GlobalXPMultiplier; }

private:
    /**
     * Resolves the combined XP multiplier for the given player.
     * Always keyed to Instigator — NPCs and other Target Actors never contribute multipliers.
     * Result: GlobalXPMultiplier × GAS personal multiplier on Instigator's ASC (1.f if ASC absent).
     */
    float ResolveMultiplier(APlayerState* Instigator) const;

    float GlobalXPMultiplier = 1.f;

    // Bound to ULevelingComponent::OnLevelUp for audit dispatch.
    void HandleLevelUpForAudit(FGameplayTag ProgressionTag, int32 NewLevel);
};
```

---

## ShouldCreateSubsystem

```cpp
bool UProgressionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // Only create on the server / standalone. Never on client-only worlds.
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->GetNetMode() != NM_Client);
}
```

---

## GrantXP Flow

```cpp
void UProgressionSubsystem::GrantXP(
    APlayerState* Instigator, AActor* Target,
    FGameplayTag ProgressionTag, int32 BaseAmount,
    int32 ContentLevel, TScriptInterface<ISourceIDInterface> Source)
{
    if (!HasAuthority()) return;
    if (!Instigator) return;  // Instigator is required for multipliers and audit.

    // Default target to instigator's pawn.
    if (!Target)
        Target = Instigator->GetPawn();

    if (!Target) return;

    ULevelingComponent* LevelingComp = Target->FindComponentByClass<ULevelingComponent>();
    if (!LevelingComp) return;  // Silent no-op — Target is not a progressable Actor.

    // Multipliers are always resolved from the Instigator (player-driven).
    const float CombinedMultiplier = ResolveMultiplier(Instigator);
    const int32 WarXP              = FMath::RoundToInt(BaseAmount * CombinedMultiplier);

    if (WarXP == 0) return;

    // Bind audit to this component if not already bound.
    // (OnLevelUp is an intra-system delegate; only UProgressionSubsystem binds it.)
    LevelingComp->OnLevelUp.AddUObject(this, &UProgressionSubsystem::HandleLevelUpForAudit);

    LevelingComp->ApplyXP(ProgressionTag, WarXP, ContentLevel);
    const int32 FinalXP = LevelingComp->GetLastAppliedXPDelta();

    // Broadcast full Event Bus messages with Instigator populated.
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FProgressionXPChangedMessage Msg;
        Msg.Instigator     = Instigator;
        Msg.Subject        = Target;
        Msg.ProgressionTag = ProgressionTag;
        Msg.NewXP          = LevelingComp->GetXP(ProgressionTag);
        Msg.Delta          = FinalXP;
        Bus->Broadcast(GameCoreEventTags::Progression_XPChanged, Msg,
            EGameCoreEventScope::ServerOnly);
    }

    // Audit dispatch.
    FGameCoreBackend::GetAudit(TAG_Audit_Progression_XPGain).RecordEvent(
        /* Instigator */ Instigator,
        /* Target     */ Target,
        /* Tag        */ ProgressionTag,
        /* BaseAmount */ BaseAmount,
        /* WarXP      */ WarXP,
        /* FinalXP    */ FinalXP,
        /* Source     */ Source,
        /* Content    */ ContentLevel
    );
}

void UProgressionSubsystem::GrantXP(
    APlayerState* Instigator, FGameplayTag ProgressionTag,
    int32 BaseAmount, int32 ContentLevel,
    TScriptInterface<ISourceIDInterface> Source)
{
    GrantXP(Instigator, nullptr, ProgressionTag, BaseAmount, ContentLevel, Source);
}
```

---

## ResolveMultiplier

```cpp
float UProgressionSubsystem::ResolveMultiplier(APlayerState* Instigator) const
{
    float Combined = GlobalXPMultiplier;

    // Personal multiplier from GAS (optional).
    if (Instigator)
    {
        if (UAbilitySystemComponent* ASC = Instigator->FindComponentByClass<UAbilitySystemComponent>())
        {
            bool bFound = false;
            const float Personal = ASC->GetGameplayAttributeValue(
                UMyProgressionAttributeSet::GetXPMultiplierAttribute(), bFound);
            if (bFound)
                Combined *= Personal;
        }
    }

    return Combined;
}
```

> The GAS attribute (`GetXPMultiplierAttribute`) is defined in the game module's AttributeSet, not in GameCore.

---

## HandleLevelUpForAudit

```cpp
void UProgressionSubsystem::HandleLevelUpForAudit(
    FGameplayTag ProgressionTag, int32 NewLevel)
{
    FGameCoreBackend::GetAudit(TAG_Audit_Progression_LevelUp).RecordEvent(
        /* ProgressionTag */ ProgressionTag,
        /* NewLevel       */ NewLevel
        // Instigator / Target not available here — full attribution is in the XPGain audit entry.
    );

    // DO NOT call URequirementWatcherManager here.
    // Watcher integration is external via the Event Bus (GameCoreEvent.Progression.LevelUp).
}
```

---

## Global XP Multiplier

```cpp
ProgressionSubsystem->SetGlobalXPMultiplier(2.f); // activate double-XP event
ProgressionSubsystem->SetGlobalXPMultiplier(1.f); // revert
```

Server-side policy only — not replicated. Inform clients via your own event/notification system.

---

## Notes

- `GrantXP` is C++ only (`BlueprintCallable` not required — server-side API). Add `BlueprintCallable` in the game module's wrapper if Blueprint access is needed.
- Multipliers are **always resolved from the Instigator**. Target Actors such as NPCs or crew never contribute their own multipliers — player bonuses scale the reward.
- The subsystem broadcasts all Event Bus messages (XPChanged, LevelUp) **after** `ApplyXP` returns, ensuring `Instigator` is always populated in the message.
- Binding `OnLevelUp` inside `GrantXP` per call may result in duplicate bindings if the same `LevelingComponent` is used repeatedly. Use `AddUnique` or bind once at actor registration.
