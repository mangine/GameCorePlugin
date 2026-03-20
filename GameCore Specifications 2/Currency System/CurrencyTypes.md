# CurrencyTypes

**File:** `Currency/CurrencyTypes.h`

All shared enums and structs for the Currency System. No dependencies on other GameCore systems.

---

## EWalletMutationResult

Returned by every mutation on `UCurrencySubsystem`. Callers must inspect this — never assume success.

```cpp
UENUM(BlueprintType)
enum class EWalletMutationResult : uint8
{
    // Mutation applied successfully.
    Success,

    // The resulting amount would violate the configured Min or Max.
    // No mutation was applied.
    ClampViolation,

    // The source wallet lacks sufficient funds to cover the requested amount.
    // Specific case of ClampViolation for spends/transfers — separated for clear caller messaging.
    InsufficientFunds,

    // The wallet component pointer was null, the actor has no authority,
    // or the wallet has no Definition assigned.
    InvalidWallet,

    // The requested currency tag is not present in this wallet's UCurrencyWalletDefinition.
    CurrencyNotConfigured,

    // Transfer only: the destination wallet cannot receive the amount due to its Max clamp.
    RecipientClampViolation,
};
```

---

## FCurrencySlotConfig

Per-currency configuration stored inside `UCurrencyWalletDefinition::Slots`. Defines the legal value range and the starting amount.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FCurrencySlotConfig
{
    GENERATED_BODY()

    // Starting amount applied only when the ledger entry is first created (never on reconnect).
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    int64 InitialAmount = 0;

    // Minimum allowed amount. Set below 0 only to allow debt mechanics.
    // Default 0 — standard wallets cannot go negative.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    int64 Min = 0;

    // Maximum allowed amount.
    // Default INT64_MAX — effectively uncapped. Set explicitly for premium currencies.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    int64 Max = TNumericLimits<int64>::Max();
};
```

**Notes:**
- `Min < 0` is the only mechanism for negative balances. Do not use on standard player wallets.
- **Set an explicit `Max` for premium currencies** (e.g. gems capped at 1,000,000) to limit exploit impact.
- Do **not** configure `Min == Max` — that creates a zero-capacity wallet and all mutations return `ClampViolation`.

---

## FCurrencyLedgerEntry *(FastArray Element)*

The replicated runtime state for one currency slot on a wallet component.

```cpp
USTRUCT()
struct GAMECORE_API FCurrencyLedgerEntry : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag CurrencyTag;

    // Signed int64 — supports negative balances when Config.Min < 0.
    UPROPERTY()
    int64 Amount = 0;

    // FastArray callbacks — forward to the owning component's OnCurrencyChanged delegate.
    void PostReplicatedAdd   (const struct FCurrencyLedger& InArraySerializer);
    void PostReplicatedChange(const struct FCurrencyLedger& InArraySerializer);
    void PreReplicatedRemove (const struct FCurrencyLedger& InArraySerializer);
};
```

---

## FCurrencyLedger *(FastArray Container)*

```cpp
USTRUCT()
struct GAMECORE_API FCurrencyLedger : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FCurrencyLedgerEntry> Items;

    // Back-pointer to owning component for delegate dispatch inside FastArray callbacks.
    // Not replicated. Set at BeginPlay by UCurrencyWalletComponent.
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

**FastArray callback implementations** (in `CurrencyTypes.h` inline or `CurrencyWalletComponent.cpp`):

```cpp
void FCurrencyLedgerEntry::PostReplicatedAdd(const FCurrencyLedger& InSerializer)
{
    if (InSerializer.OwningComponent)
        InSerializer.OwningComponent->OnCurrencyChanged.Broadcast(CurrencyTag, 0, Amount);
}

void FCurrencyLedgerEntry::PostReplicatedChange(const FCurrencyLedger& InSerializer)
{
    // OldValue is not stored on the entry; pass 0 as a sentinel on client.
    // Listeners that need the old value must cache it themselves.
    if (InSerializer.OwningComponent)
        InSerializer.OwningComponent->OnCurrencyChanged.Broadcast(CurrencyTag, 0, Amount);
}

void FCurrencyLedgerEntry::PreReplicatedRemove(const FCurrencyLedger& InSerializer)
{
    // Broadcast with NewAmount = 0 when a slot is removed.
    if (InSerializer.OwningComponent)
        InSerializer.OwningComponent->OnCurrencyChanged.Broadcast(CurrencyTag, Amount, 0);
}
```

---

## Gameplay Tag Namespaces

```ini
; DefaultGameplayTags.ini — GameCore module
[/Script/GameplayTags.GameplayTagsList]
; Currency identifiers
+GameplayTagList=(Tag="Currency.Gold")
+GameplayTagList=(Tag="Currency.Gems")
+GameplayTagList=(Tag="Currency.Reputation.Pirate")
; Audit events
+GameplayTagList=(Tag="Audit.Currency.Modify")
+GameplayTagList=(Tag="Audit.Currency.Transfer")
+GameplayTagList=(Tag="Audit.Currency.TransferCommit")
+GameplayTagList=(Tag="Audit.Currency.Recovery")
; Source identity
+GameplayTagList=(Tag="Source.Admin.Recovery")
```

Native tag handles cached at module startup in `GameCoreEventTags.h` / `FGameCoreModule::StartupModule`:

```cpp
namespace GameCoreCurrencyTags
{
    GAMECORE_API extern FGameplayTag Currency_Gold;
    GAMECORE_API extern FGameplayTag Currency_Gems;
    GAMECORE_API extern FGameplayTag Audit_Currency_Modify;
    GAMECORE_API extern FGameplayTag Audit_Currency_Transfer;
    GAMECORE_API extern FGameplayTag Audit_Currency_TransferCommit;
    GAMECORE_API extern FGameplayTag Audit_Currency_Recovery;
    GAMECORE_API extern FGameplayTag Source_Admin_Recovery;
}
```
