# UPointPoolComponent

**File:** `GameCore/Source/GameCore/Progression/PointPoolComponent.h/.cpp`

## Overview

`UPointPoolComponent` is a standalone replicated `UActorComponent` that tracks named point pools on an Actor. It has **zero knowledge of leveling** — any system calls `AddPoints` or `ConsumePoints` directly.

Mutations fire an intra-system delegate and broadcast to the **Event Bus** for external consumers. External systems must listen via the Event Bus.

All mutations are **server-only**. Clients receive delta-replicated pool state.

---

## Dependencies
- `IPersistableComponent` — binary save/load via the Serialization System
- `UGameCoreEventBus` — Event Bus broadcast target

---

## Class Definition

```cpp
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UPointPoolComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()

public:
    UPointPoolComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // -------------------------------------------------------------------------
    // Registration
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void RegisterPool(FGameplayTag PoolTag, int32 Cap = 0);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void UnregisterPool(FGameplayTag PoolTag);

    // -------------------------------------------------------------------------
    // Mutations (server-only)
    // -------------------------------------------------------------------------

    // Adds points to the pool. Returns EPointAddResult::PartialCap if the cap
    // was hit and some points were lost — caller should log a warning.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    EPointAddResult AddPoints(FGameplayTag PoolTag, int32 Amount);

    // Spends points from the pool. Returns false if insufficient spendable balance.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    bool ConsumePoints(FGameplayTag PoolTag, int32 Amount);

    // -------------------------------------------------------------------------
    // Read API (safe on client)
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Points")
    int32 GetSpendable(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintCallable, Category = "Points")
    int32 GetConsumed(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintCallable, Category = "Points")
    bool IsPoolRegistered(FGameplayTag PoolTag) const;

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
    // Delegate — INTRA-SYSTEM ONLY
    // External systems must use GameCoreEvent.Progression.PointPoolChanged
    // -------------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Points|Delegates")
    FOnPointPoolChanged OnPoolChanged;
    // Signature: (FGameplayTag PoolTag, int32 NewSpendable, int32 Delta)

private:
    UPROPERTY(Replicated)
    FPointPoolDataArray PoolData;

    FPointPoolData* FindPool(FGameplayTag Tag);
    const FPointPoolData* FindPool(FGameplayTag Tag) const;

    // Fires the intra-system delegate and the Event Bus broadcast after any mutation.
    void NotifyPoolChanged(FGameplayTag PoolTag, int32 NewSpendable, int32 Delta);
};
```

---

## AddPoints

```cpp
EPointAddResult UPointPoolComponent::AddPoints(FGameplayTag PoolTag, int32 Amount)
{
    FPointPoolData* Pool = FindPool(PoolTag);
    if (!Pool) return EPointAddResult::PoolNotFound;

    EPointAddResult Result = EPointAddResult::Success;

    if (!Pool->CanAdd(Amount))
    {
        // Clamp to remaining cap.
        Amount = (Pool->Cap > 0) ? FMath::Max(Pool->Cap - Pool->Available, 0) : Amount;
        Result = EPointAddResult::PartialCap;
    }

    if (Amount <= 0 && Result == EPointAddResult::PartialCap)
        return Result;  // Fully capped — nothing added.

    Pool->Available += Amount;
    PoolData.MarkItemDirty(*Pool);
    NotifyPoolChanged(PoolTag, Pool->GetSpendable(), Amount);
    return Result;
}
```

---

## ConsumePoints

```cpp
bool UPointPoolComponent::ConsumePoints(FGameplayTag PoolTag, int32 Amount)
{
    FPointPoolData* Pool = FindPool(PoolTag);
    if (!Pool) return false;
    if (Pool->GetSpendable() < Amount) return false;

    Pool->Consumed += Amount;
    PoolData.MarkItemDirty(*Pool);
    NotifyPoolChanged(PoolTag, Pool->GetSpendable(), -Amount);
    return true;
}
```

---

## NotifyPoolChanged

```cpp
void UPointPoolComponent::NotifyPoolChanged(FGameplayTag PoolTag, int32 NewSpendable, int32 Delta)
{
    // 1. Intra-system delegate.
    OnPoolChanged.Broadcast(PoolTag, NewSpendable, Delta);

    // 2. Event Bus — all external consumers listen here.
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FProgressionPointPoolChangedMessage Msg;
        Msg.Subject      = GetOwner();
        Msg.PoolTag      = PoolTag;
        Msg.NewSpendable = NewSpendable;
        Msg.Delta        = Delta;
        Bus->Broadcast(GameCoreEventTags::Progression_PointPoolChanged, Msg,
            EGameCoreEventScope::ServerOnly);
    }
}
```

---

## Available vs Consumed — Design Rationale

| Concern | How Addressed |
|---|---|
| Respec | Zero `Consumed` without touching `Available` — lifetime grants never change |
| Audit | Both fields always queryable — earned vs spent visible at any time |
| Cap enforcement | Cap applies only to `Available`; spending is cap-free |
| UI | HUD can display total earned, total spent, and spendable balance independently |

---

## Replication Design

| Data | Strategy | Notes |
|---|---|---|
| `PoolData` | `FFastArraySerializer` | Delta-compressed per pool entry |
| Pool change (external) | `GameCoreEvent.Progression.PointPoolChanged` via Event Bus | Server broadcasts |
| Pool change (internal) | `OnPoolChanged` delegate | Intra-system only |

---

## Pool Tag Convention (Project-Defined)

| Tag | Purpose |
|---|---|
| `Points.Skill` | General skill tree points |
| `Points.Attribute` | Stat allocation points |
| `Points.Talent` | Prestige / talent tree points |
| `Points.Reputation.*` | Per-faction reputation spending points |
