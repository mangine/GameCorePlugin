# UCurrencyWalletComponent

**File:** `Currency/CurrencyWalletComponent.h / .cpp`  
**Base:** `UActorComponent`, `IPersistableComponent`

The runtime state owner for all currency on an Actor. Attaches to any Actor that needs to hold currency. **Pure state** — never mutates itself. All mutations flow through `UCurrencySubsystem`. Implements `IPersistableComponent` for persistent wallets.

---

## Class Declaration

```cpp
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UCurrencyWalletComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()

public:
    UCurrencyWalletComponent();

    // --- Configuration ---

    // Assigned in editor or at construction. Determines supported currencies and clamp configs.
    // Must be set before BeginPlay. Null = no currencies accepted (logs error on server).
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    TObjectPtr<UCurrencyWalletDefinition> Definition;

    // --- Runtime State (server-authoritative, replicated) ---

    UPROPERTY(Replicated)
    FCurrencyLedger Ledger;

    // --- Delegates ---

    // Fired when any currency amount changes.
    // On server: fires immediately after mutation in UCurrencySubsystem.
    // On owning client: fires from FastArray PostReplicatedAdd/Change callbacks.
    // Parameters: CurrencyTag, OldAmount, NewAmount.
    // NOTE: OldAmount is always 0 on client (not stored in the replicated entry).
    DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnCurrencyChanged, FGameplayTag, int64, int64);
    FOnCurrencyChanged OnCurrencyChanged;

    // --- Read API (safe to call on client and server) ---

    // Returns current amount for the given currency. Returns 0 if not configured or not initialized.
    UFUNCTION(BlueprintCallable, Category = "Currency")
    int64 GetAmount(FGameplayTag CurrencyTag) const;

    // Returns true if this wallet has at least Amount above its configured minimum.
    // i.e. (CurrentAmount - Config.Min) >= Amount
    UFUNCTION(BlueprintCallable, Category = "Currency")
    bool CanAfford(FGameplayTag CurrencyTag, int64 Amount) const;

    // Returns true if the given currency tag is configured in the definition.
    UFUNCTION(BlueprintCallable, Category = "Currency")
    bool SupportsCurrency(FGameplayTag CurrencyTag) const;

    // --- IPersistableComponent ---
    virtual FName   GetPersistenceKey() const override { return TEXT("CurrencyWallet"); }
    virtual uint32  GetSchemaVersion()  const override { return 1; }
    virtual void    Serialize_Save(FArchive& Ar) override;
    virtual void    Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void    ClearIfSaved(uint32 FlushedGeneration) override;

    // Required dirty-tracking state (IPersistableComponent contract)
    bool   bDirty          = false;
    uint32 DirtyGeneration = 0;
    mutable TWeakObjectPtr<UPersistenceRegistrationComponent> CachedRegComp;

    // --- Lifecycle ---
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    // Returns a mutable pointer to the ledger entry for the given tag.
    // Creates a new default entry if missing. Called by UCurrencySubsystem only.
    FCurrencyLedgerEntry* GetOrCreateEntry(FGameplayTag CurrencyTag);

    // Dirty propagation helper — call after any ledger mutation.
    void NotifyDirty();

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

    // Default: replicate to owning client only.
    // Subclass and override for guild/bank/trade wallets requiring different conditions.
    DOREPLIFETIME_CONDITION(UCurrencyWalletComponent, Ledger, COND_OwnerOnly);
}
```

**Replication condition guidance:**
- **Player wallets** (`APlayerState`): `COND_OwnerOnly`. Player balance is private.
- **Guild/bank wallets**: Expose a separate replicated summary struct on the guild actor — not the raw ledger. Route only to authorized members.
- **Trade wallets**: Custom condition on the trade actor to send only to both trade participants.

---

## BeginPlay

```cpp
void UCurrencyWalletComponent::BeginPlay()
{
    Super::BeginPlay();

    // Only the server initializes state. Clients receive it via replication.
    if (!GetOwner()->HasAuthority())
        return;

    if (!Definition)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Economy)->LogError(
            TEXT("CurrencyWallet"),
            FString::Printf(TEXT("%s: UCurrencyWalletComponent has no Definition assigned."),
                *GetOwner()->GetName()));
        return;
    }

    // Initialize ledger entries from definition.
    // Skips slots that persistence has already restored (entries already present).
    for (const auto& [Tag, Config] : Definition->Slots)
    {
        if (!Ledger.Items.ContainsByPredicate(
            [&](const FCurrencyLedgerEntry& E) { return E.CurrencyTag == Tag; }))
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
        if (Entry.CurrencyTag == CurrencyTag)
            return Entry.Amount;
    return 0;
}

bool UCurrencyWalletComponent::CanAfford(FGameplayTag CurrencyTag, int64 Amount) const
{
    if (!Definition) return false;
    const FCurrencySlotConfig* Config = Definition->FindSlotConfig(CurrencyTag);
    if (!Config) return false;
    return (GetAmount(CurrencyTag) - Config->Min) >= Amount;
}

bool UCurrencyWalletComponent::SupportsCurrency(FGameplayTag CurrencyTag) const
{
    return Definition && Definition->FindSlotConfig(CurrencyTag) != nullptr;
}
```

---

## Persistence Implementation

```cpp
void UCurrencyWalletComponent::Serialize_Save(FArchive& Ar)
{
    // Strictly read-only — no state mutation during save.
    int32 Count = Ledger.Items.Num();
    Ar << Count;
    for (FCurrencyLedgerEntry& Entry : Ledger.Items)
    {
        FString TagStr = Entry.CurrencyTag.ToString();
        Ar << TagStr;
        Ar << Entry.Amount;
    }
}

void UCurrencyWalletComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    int32 Count;
    Ar << Count;
    Ledger.Items.Empty(Count);
    for (int32 i = 0; i < Count; ++i)
    {
        FString TagStr;
        Ar << TagStr;
        int64 Amount;
        Ar << Amount;

        FCurrencyLedgerEntry& Entry = Ledger.Items.AddDefaulted_GetRef();
        Entry.CurrencyTag = FGameplayTag::RequestGameplayTag(*TagStr);
        Entry.Amount      = Amount;
    }
    // Do not call MarkItemDirty here — replication handles it after restore.
}

void UCurrencyWalletComponent::ClearIfSaved(uint32 FlushedGeneration)
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
}

void UCurrencyWalletComponent::NotifyDirty()
{
    if (bDirty) return;
    if (!CachedRegComp.IsValid())
        CachedRegComp = GetOwner()->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (CachedRegComp.IsValid())
    {
        DirtyGeneration = CachedRegComp->SaveGeneration;
        bDirty = true;
        CachedRegComp->MarkDirty();
    }
    // If no UPersistenceRegistrationComponent: silently skip (trade/ephemeral wallets).
}
```

**Persistence registration rules:**
- **Player wallets** (`APlayerState`): Actor has a `UPersistenceRegistrationComponent` with `BackendTag = TAG_Persistence_Entity_Player`. Wallet auto-discovered at `BeginPlay`.
- **Guild/bank wallets**: Actor has `UPersistenceRegistrationComponent` with `BackendTag = TAG_Persistence_Entity_Economy`.
- **Trade/ephemeral wallets**: **No** `UPersistenceRegistrationComponent`. `NotifyDirty` silently no-ops. Crash recovery is via audit log replay.

---

## One Component Per Actor Rule

An Actor must have **at most one** `UCurrencyWalletComponent`. If an entity logically needs separate pools (e.g. a bank with personal and guild vaults), use two distinct Actors — not two components on the same Actor. Two components on the same Actor would produce duplicate `GetPersistenceKey()` collisions in the persistence system.
