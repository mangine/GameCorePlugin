# UPointPoolComponent

## Overview

`UPointPoolComponent` is a replicated `UActorComponent` that manages one or more named point pools on an Actor. Each pool is identified by a `FGameplayTag` and tracks available (spendable) and consumed points separately. The component is a standalone state owner — it has no knowledge of `ULevelingComponent` or any other system. Any system may call `AddPoints` or `ConsumePoints` directly.

All mutations are **server-only**. The client receives delta-replicated state via FastArray.

## Plugin Module

`GameCore` (runtime module)

## File Location

```
GameCore/Source/GameCore/Progression/
└── PointPoolComponent.h / .cpp
```

## Dependencies

- `IPersistableComponent` — implemented for binary save/load via the Serialization System.
- `UGameCoreEventSubsystem` — broadcast target for cross-system pool change events.

---

## Class Definition

```cpp
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UPointPoolComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()

public:
    // ── Registration ─────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void RegisterPool(FGameplayTag PoolTag, int32 Cap = INDEX_NONE);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void UnregisterPool(FGameplayTag PoolTag);

    // ── Mutation ──────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    EPointAddResult AddPoints(FGameplayTag PoolTag, int32 Amount);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    bool ConsumePoints(FGameplayTag PoolTag, int32 Amount);

    // ── Queries ───────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Points")
    int32 GetSpendable(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintCallable, Category = "Points")
    int32 GetConsumed(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintCallable, Category = "Points")
    bool IsPoolRegistered(FGameplayTag PoolTag) const;

    // ── Persistence ───────────────────────────────────────────────────────────

    virtual void SerializeForSave(FArchive& Ar) override;
    virtual void DeserializeFromSave(FArchive& Ar) override;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    FString SerializeToString() const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    void DeserializeFromString(const FString& Data);

    // ── Delegates  (intra-system only) ────────────────────────────────────────

    // Fired whenever a pool's available or consumed count changes.
    // External systems must use GMS channel GameCoreEvent.Progression.PointPoolChanged instead.
    UPROPERTY(BlueprintAssignable, Category = "Points|Delegates")
    FOnPointPoolChanged OnPoolChanged;
    // Signature: (FGameplayTag PoolTag, int32 NewSpendable, int32 Delta)

private:
    UPROPERTY(Replicated)
    FPointPoolDataArray PoolData;

    FPointPoolData* FindPool(FGameplayTag Tag);
    const FPointPoolData* FindPool(FGameplayTag Tag) const;

    // Broadcasts OnPoolChanged delegate and GMS message after any mutation.
    void NotifyPoolChanged(FGameplayTag PoolTag, int32 NewSpendable, int32 Delta);
};
```

---

## Mutation and Notification Pattern

All mutations route through `NotifyPoolChanged` after applying the change:

```cpp
void UPointPoolComponent::NotifyPoolChanged(FGameplayTag PoolTag, int32 NewSpendable, int32 Delta)
{
    // 1. Intra-system delegate.
    OnPoolChanged.Broadcast(PoolTag, NewSpendable, Delta);

    // 2. GMS broadcast for external consumers.
    if (UGameCoreEventSubsystem* EventBus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
    {
        FProgressionPointPoolChangedMessage Msg;
        Msg.PlayerState   = GetOwner<APlayerState>();
        Msg.PoolTag       = PoolTag;
        Msg.NewSpendable  = NewSpendable;
        Msg.Delta         = Delta;
        EventBus->Broadcast(GameCoreEventTags::Progression_PointPoolChanged, Msg);
    }
}
```

---

## EPointAddResult

```cpp
UENUM(BlueprintType)
enum class EPointAddResult : uint8
{
    Success,        // All points added
    PartialCap,     // Some points lost due to cap
    PoolNotFound,   // Tag not registered
};
```

The caller logs a warning on `PartialCap` so designers know their grant curve outpaces the configured cap.

---

## Replication Design

| Data | Replication Strategy | Notes |
| --- | --- | --- |
| `PoolData` (available + consumed + cap) | `FFastArraySerializer` | Delta-compressed per-pool entry |
| Mutation calls | `BlueprintAuthorityOnly` | Clients can never add or consume points |
| Pool change notification (external) | `GameCoreEvent.Progression.PointPoolChanged` via GMS | Server broadcasts |
| Pool change notification (internal) | `OnPoolChanged` delegate | Intra-system only |

---

## Design: Available vs Consumed Tracking

- **Audit** — total lifetime grants vs total spent always visible.
- **Refund support** — a respec system zeros `Consumed` without touching `Available`.
- **Cap enforcement** — cap applies to `Available` only; spending is always cap-free.
- **UI** — HUD can show both earned and spent separately.

---

## Multiple Grant Sources

```cpp
// From a quest reward
PoolComp->AddPoints(FGameplayTag::RequestGameplayTag("Points.Skill"), 3);

// From a seasonal event
PoolComp->AddPoints(FGameplayTag::RequestGameplayTag("Points.Talent"), 1);

// From ULevelingComponent on level-up (internal)
PoolComp->AddPoints(Grant.PoolTag, Grant.EvaluateForLevel(NewLevel));
```

---

## Pool Tag Convention

| Tag | Purpose |
| --- | --- |
| `Points.Skill` | General skill tree points |
| `Points.Attribute` | Stat allocation points |
| `Points.Talent` | Prestige / talent tree points |
| `Points.Reputation.*` | Per-faction reputation points |
