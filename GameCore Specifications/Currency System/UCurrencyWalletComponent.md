# UCurrencyWalletComponent

**Sub-page of:** [Currency System](../Currency%20System.md)

**File:** `Currency/CurrencyWalletComponent.h / .cpp`

The state owner for all currency on an Actor. Attaches to any Actor that needs to hold currency — `APlayerState`, guild actors, trade session actors, bank actors. Pure state: it never mutates itself. All mutations flow through `UCurrencySubsystem`.

---

## Class Declaration

```cpp
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UCurrencyWalletComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCurrencyWalletComponent();

    // --- Configuration ---

    // Assigned in the editor or at construction.
    // Determines which currencies this wallet supports and their clamp configs.
    // Must be set before BeginPlay. Null = no currencies accepted.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    TObjectPtr<UCurrencyWalletDefinition> Definition;

    // --- Runtime State (server-authoritative, replicated) ---

    UPROPERTY(Replicated)
    FCurrencyLedger Ledger;

    // --- Delegates (fired on owning client after replication, and on server after mutation) ---

    // Fired when any currency amount changes.
    // Parameters: CurrencyTag, OldAmount, NewAmount.
    DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnCurrencyChanged, FGameplayTag, int64, int64);
    FOnCurrencyChanged OnCurrencyChanged;

    // --- Read API (safe to call on client and server) ---

    // Returns current amount for the given currency. Returns 0 if not configured.
    UFUNCTION(BlueprintCallable, Category = "Currency")
    int64 GetAmount(FGameplayTag CurrencyTag) const;

    // Returns true if this wallet has at least Amount of the given currency above its minimum.
    // i.e. (CurrentAmount - Config.Min) >= Amount
    UFUNCTION(BlueprintCallable, Category = "Currency")
    bool CanAfford(FGameplayTag CurrencyTag, int64 Amount) const;

    // Returns true if the given currency tag is configured in the definition.
    UFUNCTION(BlueprintCallable, Category = "Currency")
    bool SupportsCurrency(FGameplayTag CurrencyTag) const;

    // --- Lifecycle ---

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    // Called by UCurrencySubsystem only. Not BlueprintCallable.
    // Returns a mutable pointer to the ledger entry for the given tag.
    // Creates the entry if missing (first mutation for this tag).
    FCurrencyLedgerEntry* GetOrCreateEntry(FGameplayTag CurrencyTag);

    friend class UCurrencySubsystem;
};
```

---

## Replication

```cpp
void UCurrencyWalletComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // Player wallets: replicate to owning client only.
    // For shared wallets (guild, bank), override this in the owning Actor
    // or use a subclass with COND_Custom and a relevancy check.
    DOREPLIFETIME_CONDITION(UCurrencyWalletComponent, Ledger, COND_OwnerOnly);
}
```

**Notes on replication conditions:**
- **Player wallets** (`APlayerState`): `COND_OwnerOnly`. The player's own currency is private.
- **Guild/bank wallets**: Expose a separate replicated summary struct on the guild actor — not the raw ledger. Only authorized members receive it.
- **Trade wallets**: The trade session actor replicates to both participants only via a custom relevancy condition.

---

## BeginPlay Initialization

```cpp
void UCurrencyWalletComponent::BeginPlay()
{
    Super::BeginPlay();

    // Only the server initializes state. Clients receive it via replication.
    if (!GetOwner()->HasAuthority())
        return;

    if (!Definition)
    {
        UE_LOG(LogCurrency, Error, TEXT("%s: UCurrencyWalletComponent has no Definition assigned."),
            *GetOwner()->GetName());
        return;
    }

    // Initialize ledger entries from definition.
    // If a persistence system has already restored state (e.g. from PlayerDB),
    // this is a no-op for slots already present — only adds missing slots.
    for (const auto& [Tag, Config] : Definition->Slots)
    {
        if (!Ledger.Items.ContainsByPredicate([&](const FCurrencyLedgerEntry& E)
            { return E.CurrencyTag == Tag; }))
        {
            FCurrencyLedgerEntry& Entry = Ledger.Items.AddDefaulted_GetRef();
            Entry.CurrencyTag = Tag;
            Entry.Amount      = Config.InitialAmount;
            Ledger.MarkItemDirty(Entry);
        }
    }

    Ledger.OwningComponent = this;
}
```

---

## Read API Implementation

```cpp
int64 UCurrencyWalletComponent::GetAmount(FGameplayTag CurrencyTag) const
{
    for (const FCurrencyLedgerEntry& Entry : Ledger.Items)
    {
        if (Entry.CurrencyTag == CurrencyTag)
            return Entry.Amount;
    }
    return 0;
}

bool UCurrencyWalletComponent::CanAfford(FGameplayTag CurrencyTag, int64 Amount) const
{
    if (!Definition) return false;
    const FCurrencySlotConfig* Config = Definition->FindSlotConfig(CurrencyTag);
    if (!Config) return false;
    return (GetAmount(CurrencyTag) - Config->Min) >= Amount;
}
```

---

## Persistence Integration

`UCurrencyWalletComponent` implements `IPersistableComponent` for player wallets. The ledger serializes all `FCurrencyLedgerEntry` items by tag.

- **Player wallets**: register with `UPersistenceSubsystem`, backend key `"PlayerDB"`.
- **Guild/bank wallets**: register with backend key `"EconomyDB"` from the owning actor's `BeginPlay`.
- **Trade wallets**: **do not register** with `UPersistenceSubsystem`. Ephemeral by design. Crash recovery uses audit log replay.

---

## One Component Per Actor Rule

An Actor should have at most one `UCurrencyWalletComponent`. If an entity logically needs separate pools (e.g. a bank with personal and guild vaults), use two distinct Actors, not two components on the same Actor.
