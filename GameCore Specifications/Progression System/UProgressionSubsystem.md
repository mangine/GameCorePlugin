# UProgressionSubsystem

**Sub-page of:** [Progression System](../Progression%20System%2031bd261a36cf8139a371f3c7327ae7d8.md)

## Overview

`UProgressionSubsystem` is a **server-only** `UWorldSubsystem` that acts as the sole external entry point for all XP grants. It owns the policy layer — multiplier resolution, audit dispatch, and watcher notification — while components remain pure state owners.

Gameplay code and systems such as `RewardResolver` never call `ULevelingComponent` directly for XP mutations. They always go through this subsystem.

## Plugin Module

`GameCore` (runtime module)

## File Location

```
GameCore/Source/GameCore/Progression/
└── ProgressionSubsystem.h / .cpp
```

## Dependencies

- `ULevelingComponent` — resolved at runtime via `PS->GetPawn()->FindComponentByClass`. Never stored as a hard reference.
- `UAbilitySystemComponent` — queried for personal XP multiplier attribute. Soft dependency; skipped if ASC not present.
- `URequirementWatcherManager` — notified post-mutation via `GetWorld()->GetSubsystem<URequirementWatcherManager>()`.
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

    // -------------------------------------------------------------------------
    // Primary Entry Point
    // -------------------------------------------------------------------------

    /**
     * Grants XP to a player's progression track.
     * Resolves multipliers, delegates final XP application to ULevelingComponent,
     * then dispatches audit and watcher notifications.
     *
     * Server-only. Calling from client is a no-op.
     *
     * @param PS              Target player state.
     * @param ProgressionTag  Which progression receives the XP.
     * @param BaseAmount      Raw XP before any multipliers or reduction.
     * @param ContentLevel    Level of the content that triggered the grant.
     *                        Pass INDEX_NONE to skip level-based reduction entirely.
     * @param Source          Optional source identifier for audit/telemetry.
     */
    void GrantXP(
        APlayerState* PS,
        FGameplayTag ProgressionTag,
        int32 BaseAmount,
        int32 ContentLevel,
        TScriptInterface<ISourceIDInterface> Source
    );

    // -------------------------------------------------------------------------
    // Global Multiplier — set by live-ops / event system
    // -------------------------------------------------------------------------

    void  SetGlobalXPMultiplier(float Multiplier);
    float GetGlobalXPMultiplier() const { return GlobalXPMultiplier; }

private:
    // Combines GlobalXPMultiplier with the player's personal GAS multiplier attribute.
    // Returns 1.f if the player has no ASC or no XP multiplier attribute set.
    float ResolveMultipliers(APlayerState* PS, FGameplayTag ProgressionTag) const;

    // Builds and dispatches an FAuditEntry via FGameCoreBackend post-mutation.
    void DispatchAudit(
        APlayerState* PS,
        FGameplayTag ProgressionTag,
        int32 WarXP,
        int32 FinalXP,
        FGameplayTag SourceTag,
        TScriptInterface<ISourceIDInterface> Source
    );

    // Notifies URequirementWatcherManager of the XP/level change event.
    void NotifyWatcher(APlayerState* PS, FGameplayTag ProgressionTag);

    float GlobalXPMultiplier = 1.f;
};
```

---

## Server-Only Gate

`ShouldCreateSubsystem` returns false on clients:

```cpp
bool UProgressionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->GetNetMode() != NM_Client;
}
```

---

## GrantXP Flow

```cpp
void UProgressionSubsystem::GrantXP(
    APlayerState* PS, FGameplayTag ProgressionTag,
    int32 BaseAmount, int32 ContentLevel,
    TScriptInterface<ISourceIDInterface> Source)
{
    if (!PS || !HasAuthority()) return;

    // 1. Resolve war XP (global × personal multipliers)
    const float CombinedMultiplier = ResolveMultipliers(PS, ProgressionTag);
    const int32 WarXP = FMath::RoundToInt(BaseAmount * CombinedMultiplier);
    if (WarXP == 0) return;

    // 2. Delegate final XP application (reduction lives in the component)
    APawn* Pawn = PS->GetPawn();
    if (!Pawn) return;

    ULevelingComponent* LevelingComp = Pawn->FindComponentByClass<ULevelingComponent>();
    if (!LevelingComp) return;

    LevelingComp->ApplyXP(ProgressionTag, WarXP, ContentLevel);

    // 3. Audit post-mutation (records what actually happened)
    const int32 FinalXP = LevelingComp->GetLastAppliedXPDelta();  // set by ApplyXP
    const FGameplayTag SourceTag = Source.GetObject()
        ? ISourceIDInterface::Execute_GetSourceTag(Source.GetObject())
        : FGameplayTag::EmptyTag;

    DispatchAudit(PS, ProgressionTag, WarXP, FinalXP, SourceTag, Source);

    // 4. Notify watcher system for requirement re-evaluation
    NotifyWatcher(PS, ProgressionTag);
}
```

---

## Multiplier Resolution

```cpp
float UProgressionSubsystem::ResolveMultipliers(APlayerState* PS, FGameplayTag ProgressionTag) const
{
    float Personal = 1.f;

    if (APawn* Pawn = PS->GetPawn())
    {
        if (UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>())
        {
            // Designers expose a UProgressionAttributeSet::XPMultiplier attribute.
            // Default value of the attribute should be 1.0.
            const FGameplayAttribute XPAttr = UProgressionAttributeSet::GetXPMultiplierAttribute();
            if (ASC->HasAttributeSetForAttribute(XPAttr))
            {
                Personal = ASC->GetNumericAttribute(XPAttr);
            }
        }
    }

    return GlobalXPMultiplier * Personal;
}
```

---

## Audit Dispatch

Called post-mutation. Records what was actually applied, not what was requested.

```cpp
void UProgressionSubsystem::DispatchAudit(
    APlayerState* PS, FGameplayTag ProgressionTag,
    int32 WarXP, int32 FinalXP,
    FGameplayTag SourceTag, TScriptInterface<ISourceIDInterface> Source)
{
    FAuditEntry Entry;
    Entry.EventTag         = TAG_Audit_Progression_XPGain;
    Entry.SchemaVersion    = 1;
    Entry.ActorId          = PS->GetUniqueId().GetUniqueNetIdValue();  // resolved from PlayerState
    Entry.ActorDisplayName = PS->GetPlayerName();
    Entry.Payload          = FAuditPayloadBuilder{}
        .SetTag  (TEXT("progression"), ProgressionTag)
        .SetInt  (TEXT("war_xp"),      WarXP)
        .SetInt  (TEXT("final_xp"),    FinalXP)
        .SetFloat(TEXT("reduction"),   WarXP > 0 ? static_cast<float>(FinalXP) / WarXP : 1.f)
        .SetTag  (TEXT("source"),      SourceTag)
        .ToString();

    FGameCoreBackend::GetAudit(TAG_Audit_Progression)->RecordEvent(Entry);
}
```

---

## Watcher Notification

```cpp
void UProgressionSubsystem::NotifyWatcher(APlayerState* PS, FGameplayTag ProgressionTag)
{
    if (URequirementWatcherManager* Manager = GetWorld()->GetSubsystem<URequirementWatcherManager>())
    {
        Manager->NotifyPlayerEvent(PS, TAG_RequirementEvent_Progression_Changed);
    }
}
```

The watcher manager is resolved via `GetWorld()->GetSubsystem<>()` — no stored reference, no coupling.

---

## Global Multiplier

Set by the live-ops or event system (e.g. a double-XP weekend):

```cpp
// Called from your EventSubsystem or live-ops tooling
ProgressionSubsystem->SetGlobalXPMultiplier(2.f);  // 2x XP event
ProgressionSubsystem->SetGlobalXPMultiplier(1.f);  // revert
```

This is **not** replicated — it is a server-side policy value only. Clients are informed through game event messaging if needed (out of scope for this system).

---

## Notes

- `GrantXP` is not `BlueprintCallable` — it is a C++ API intended for server-side systems only (RewardResolver, quest system, GM tools via a thin Blueprint wrapper if needed).
- Level-up audit is handled separately: `UProgressionSubsystem::Initialize` subscribes to `ULevelingComponent::OnLevelUp` for each registered player. The level-up audit entry uses `TAG_Audit_Progression_LevelUp`.
- `GetLastAppliedXPDelta()` is a lightweight int32 cached by `ApplyXP` on the component — it avoids a second lookup and is reset each call.
