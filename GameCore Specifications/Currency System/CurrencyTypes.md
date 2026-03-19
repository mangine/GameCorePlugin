# Currency Types

**Sub-page of:** [Currency System](../Currency%20System.md)

**File:** `Currency/CurrencyTypes.h`

All shared enums and structs used across the Currency System. No dependencies on other GameCore systems.

---

## EWalletMutationResult

Returned by every mutation operation on `UCurrencySubsystem`. Callers must inspect this — never assume success.

```cpp
UENUM(BlueprintType)
enum class EWalletMutationResult : uint8
{
    // Mutation applied successfully.
    Success,

    // The resulting amount would violate the configured Min or Max for this currency.
    // No mutation was applied.
    ClampViolation,

    // The source wallet does not have enough funds to cover the requested amount.
    // Specific case of ClampViolation for spends/transfers — separated for clear caller messaging.
    InsufficientFunds,

    // The wallet component pointer was null or the actor is not server-authoritative.
    InvalidWallet,

    // The requested currency tag is not configured in this wallet's UCurrencyWalletDefinition.
    CurrencyNotConfigured,

    // The destination wallet (transfer only) cannot receive the requested amount due to its Max clamp.
    RecipientClampViolation,
};
```

---

## FCurrencySlotConfig

Per-currency configuration stored in `UCurrencyWalletDefinition`. Defines the legal value range and the starting amount for this slot.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FCurrencySlotConfig
{
    GENERATED_BODY()

    // Starting amount when the wallet is first initialized.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    int64 InitialAmount = 0;

    // Minimum allowed amount. May be negative (e.g. debt mechanics).
    // Default 0 — wallets cannot go below zero unless explicitly configured.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    int64 Min = 0;

    // Maximum allowed amount.
    // Default INT64_MAX — effectively uncapped.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    int64 Max = TNumericLimits<int64>::Max();
};
```

**Notes:**
- Setting `Min < 0` is the only mechanism to allow negative balances. Do not use this for standard player wallets.
- `Max` should be set conservatively for premium currencies to limit exploit impact (e.g. cap gems at 1,000,000).
- `InitialAmount` is applied only at first wallet initialization — not on respawn or reconnect.

---

## FCurrencyLedgerEntry  *(FastArray Element)*

The replicated runtime state for one currency slot on a wallet component.

```cpp
USTRUCT()
struct GAMECORE_API FCurrencyLedgerEntry : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag CurrencyTag;

    // Signed int64 — supports negative balances when config permits.
    UPROPERTY()
    int64 Amount = 0;

    // FastArray callbacks — forward to component delegates.
    void PostReplicatedAdd   (const struct FCurrencyLedger& InArraySerializer);
    void PostReplicatedChange(const struct FCurrencyLedger& InArraySerializer);
    void PreReplicatedRemove (const struct FCurrencyLedger& InArraySerializer);
};

USTRUCT()
struct GAMECORE_API FCurrencyLedger : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FCurrencyLedgerEntry> Items;

    // Pointer back to owning component for delegate dispatch in FastArray callbacks.
    // Not replicated. Set at BeginPlay.
    UPROPERTY(NotReplicated)
    TObjectPtr<UCurrencyWalletComponent> OwningComponent;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<
            FCurrencyLedgerEntry, FCurrencyLedger>(Items, DeltaParams, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FCurrencyLedger> : public TStructOpsTypeTraitsBase2<FCurrencyLedger>
{
    enum { WithNetDeltaSerializer = true };
};
```

---

## Gameplay Tag Namespaces

| Namespace | Usage |
|---|---|
| `Currency.Gold` | Primary gold currency |
| `Currency.Gems` | Premium currency |
| `Currency.Reputation.*` | Faction-specific reputation currencies |
| `Audit.Currency.Modify` | Single-wallet mutation audit event |
| `Audit.Currency.Transfer` | Transfer begin audit event |
| `Audit.Currency.TransferCommit` | Transfer completion audit event |
| `Audit.Currency.Recovery` | Crash recovery audit event |
| `Source.Admin.Recovery` | Source tag for automated crash recovery grants |
