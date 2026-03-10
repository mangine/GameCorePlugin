# UPointPoolComponent

## Overview

`UPointPoolComponent` is a **standalone replicated `UActorComponent`** that tracks named point pools on an Actor. It is the single aggregation point for all spendable point currencies — skill points, attribute points, talent points, or any custom pool defined by a `FGameplayTag`.

It has **no knowledge of leveling**. Any system — `ULevelingComponent`, quest rewards, seasonal events, GM grants — simply calls `AddPoints(Tag, Amount)`. The component tracks available vs consumed and exposes spendable balance.

All mutations are **server-only**. Clients receive delta-replicated pool state.

## Plugin Module

`GameCore` (runtime module)

## File Location

```
GameCore/Source/GameCore/Progression/
└── PointPoolComponent.h / .cpp
```

## Dependencies

- `IPersistableComponent` — implemented for binary save/load via the Serialization System.

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
    // Pool Configuration
    // -------------------------------------------------------------------------

    /**
     * Registers a pool tag so it can receive points.
     * Optional cap: 0 = no cap. Server-only.
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void RegisterPool(FGameplayTag PoolTag, int32 Cap = 0);

    // -------------------------------------------------------------------------
    // Mutations (server-only)
    // -------------------------------------------------------------------------

    /**
     * Adds points to a pool. Clamps to cap if one is set.
     * Returns EPointAddResult to inform caller if points were lost to cap.
     * Server-only.
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    EPointAddResult AddPoints(FGameplayTag PoolTag, int32 Amount);

    /**
     * Consumes (spends) points from a pool.
     * Returns false if insufficient spendable points. Server-only.
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    bool ConsumePoints(FGameplayTag PoolTag, int32 Amount);

    /**
     * Sets the cap on an existing pool. 0 removes the cap. Server-only.
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void SetPoolCap(FGameplayTag PoolTag, int32 NewCap);

    // -------------------------------------------------------------------------
    // Queries (safe to call from client)
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Points")
    int32 GetAvailable(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintPure, Category = "Points")
    int32 GetConsumed(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintPure, Category = "Points")
    int32 GetSpendable(FGameplayTag PoolTag) const;  // Available - Consumed

    UFUNCTION(BlueprintPure, Category = "Points")
    int32 GetCap(FGameplayTag PoolTag) const;  // 0 = no cap

    UFUNCTION(BlueprintPure, Category = "Points")
    bool IsPoolRegistered(FGameplayTag PoolTag) const;

    // -------------------------------------------------------------------------
    // Persistence  (IPersistableComponent)
    // -------------------------------------------------------------------------

    // Binary serialization — called by the Serialization System at snapshot time.
    virtual void SerializeForSave(FArchive& Ar) override;

    // Binary deserialization — called by the Serialization System at restore time. Server-only.
    virtual void DeserializeFromSave(FArchive& Ar) override;

    // JSON helpers — GM tooling and debug inspection only. Never called on the save path.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    FString SerializeToString() const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    void DeserializeFromString(const FString& Data);

    // -------------------------------------------------------------------------
    // Delegates
    // -------------------------------------------------------------------------

    // Fired whenever a pool's available or consumed count changes.
    UPROPERTY(BlueprintAssignable, Category = "Points|Delegates")
    FOnPointPoolChanged OnPoolChanged;
    // Signature: (FGameplayTag PoolTag, int32 NewSpendable, int32 Delta)

    // -------------------------------------------------------------------------
    // Private
    // -------------------------------------------------------------------------
private:
    UPROPERTY(Replicated)
    FPointPoolDataArray PoolData;

    FPointPoolData* FindPool(FGameplayTag Tag);
    const FPointPoolData* FindPool(FGameplayTag Tag) const;
};
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

The caller (e.g. `ULevelingComponent::GrantPointsForLevel`) should log a warning if `PartialCap` is returned, so designers know their progression grant curve is outpacing the configured cap.

---

## Replication Design

| Data | Replication Strategy | Notes |
| --- | --- | --- |
| `PoolData` (available + consumed + cap) | `FFastArraySerializer` | Delta-compressed per-pool entry |
| Mutation calls | `BlueprintAuthorityOnly` | Clients can never add or consume points |

---

## Design: Available vs Consumed Tracking

Tracking `Available` and `Consumed` separately (rather than a single `Balance`) provides several advantages:

- **Audit** — you always know total lifetime grants vs total spent.
- **Refund support** — a respec system can zero `Consumed` without touching `Available`.
- **Cap enforcement** — cap applies to `Available` only, not to `Consumed`; spending is always free of cap.
- **UI** — the HUD can show both "earned" and "spent" separately for player clarity.

---

## Multiple Grant Sources

Any system calls `AddPoints` directly — no routing through `ULevelingComponent`:

```cpp
// From a quest reward
PoolComp->AddPoints(
    FGameplayTag::RequestGameplayTag("Points.Skill"),
    3
);

// From a seasonal event
PoolComp->AddPoints(
    FGameplayTag::RequestGameplayTag("Points.Talent"),
    1
);

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
| `Points.Reputation.*` | Per-faction spend currency |

Pool tags are project-defined — GameCore ships no tag values, only the infrastructure.

---

## Usage Example

```cpp
// Setup (server, BeginPlay)
PoolComp->RegisterPool(FGameplayTag::RequestGameplayTag("Points.Skill"), 0);     // No cap
PoolComp->RegisterPool(FGameplayTag::RequestGameplayTag("Points.Attribute"), 50); // Max 50 unspent

// Spending (server, from skill tree system)
bool bSuccess = PoolComp->ConsumePoints(
    FGameplayTag::RequestGameplayTag("Points.Skill"),
    1
);
if (!bSuccess)
{
    // Not enough spendable points — reject the spend request
}

// Query (client-safe)
int32 Spendable = PoolComp->GetSpendable(
    FGameplayTag::RequestGameplayTag("Points.Skill")
);
```