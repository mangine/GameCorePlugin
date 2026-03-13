# UPointPoolComponent

## Overview

`UPointPoolComponent` is a standalone replicated `UActorComponent` that tracks named point pools on an Actor. It has no knowledge of leveling. Any system calls `AddPoints` or `ConsumePoints` directly.

All mutations are **server-only**. The client receives delta-replicated pool state.

Mutations fire an **intra-system delegate** and broadcast to **`UGameCoreEventSubsystem`** for external consumers. External systems must listen via GMS and must not bind directly to `OnPoolChanged`.

## Plugin Module

`GameCore` (runtime module)

## File Location

```
GameCore/Source/GameCore/Progression/
└── PointPoolComponent.h / .cpp
```

## Dependencies

- `IPersistableComponent` — binary save/load via the Serialization System.
- `UGameCoreEventSubsystem` — broadcast target for pool change events.

---

## Class Definition

```cpp
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UPointPoolComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void RegisterPool(FGameplayTag PoolTag, int32 Cap = 0);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void UnregisterPool(FGameplayTag PoolTag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    EPointAddResult AddPoints(FGameplayTag PoolTag, int32 Amount);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    bool ConsumePoints(FGameplayTag PoolTag, int32 Amount);

    UFUNCTION(BlueprintCallable, Category = "Points")
    int32 GetSpendable(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintCallable, Category = "Points")
    int32 GetConsumed(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintCallable, Category = "Points")
    bool IsPoolRegistered(FGameplayTag PoolTag) const;

    virtual void SerializeForSave(FArchive& Ar) override;
    virtual void DeserializeFromSave(FArchive& Ar) override;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    FString SerializeToString() const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    void DeserializeFromString(const FString& Data);

    // -------------------------------------------------------------------------
    // Delegate — INTRA-SYSTEM ONLY
    // -------------------------------------------------------------------------

    // Fired whenever a pool's available or consumed count changes.
    // External systems MUST use GMS channel GameCoreEvent.Progression.PointPoolChanged instead.
    UPROPERTY(BlueprintAssignable, Category = "Points|Delegates")
    FOnPointPoolChanged OnPoolChanged;
    // Signature: (FGameplayTag PoolTag, int32 NewSpendable, int32 Delta)

private:
    UPROPERTY(Replicated)
    FPointPoolDataArray PoolData;

    FPointPoolData* FindPool(FGameplayTag Tag);
    const FPointPoolData* FindPool(FGameplayTag Tag) const;

    // Called after every mutation. Fires delegate + GMS broadcast.
    void NotifyPoolChanged(FGameplayTag PoolTag, int32 NewSpendable, int32 Delta);
};
```

---

## Mutation and Notification Pattern

```cpp
void UPointPoolComponent::NotifyPoolChanged(FGameplayTag PoolTag, int32 NewSpendable, int32 Delta)
{
    // 1. Intra-system delegate.
    OnPoolChanged.Broadcast(PoolTag, NewSpendable, Delta);

    // 2. GMS for all external consumers.
    if (UGameCoreEventSubsystem* Bus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
    {
        FProgressionPointPoolChangedMessage Msg;
        Msg.PlayerState  = GetOwner<APlayerState>();
        Msg.PoolTag      = PoolTag;
        Msg.NewSpendable = NewSpendable;
        Msg.Delta        = Delta;
        Bus->Broadcast(GameCoreEventTags::Progression_PointPoolChanged, Msg);
    }
}
```

---

## EPointAddResult

```cpp
UENUM(BlueprintType)
enum class EPointAddResult : uint8
{
    Success,
    PartialCap,    // Some points lost to cap — caller should log a warning
    PoolNotFound,
};
```

---

## Replication Design

| Data | Replication | Notes |
| --- | --- | --- |
| `PoolData` | `FFastArraySerializer` | Delta-compressed per-pool entry |
| Pool change (external) | `GameCoreEvent.Progression.PointPoolChanged` via GMS | Server broadcasts |
| Pool change (internal) | `OnPoolChanged` delegate | Intra-system only |

---

## Design: Available vs Consumed Tracking

- **Audit** — lifetime grants vs spent always queryable.
- **Refund support** — respec zeros `Consumed` without touching `Available`.
- **Cap enforcement** — cap applies to `Available` only; spending is cap-free.
- **UI** — HUD can show both earned and spent separately.

---

## Pool Tag Convention

| Tag | Purpose |
| --- | --- |
| `Points.Skill` | General skill tree points |
| `Points.Attribute` | Stat allocation points |
| `Points.Talent` | Prestige / talent tree points |
| `Points.Reputation.*` | Per-faction reputation points |
