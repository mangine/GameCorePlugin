# UCurrencySubsystem

**Sub-page of:** [Currency System](../Currency%20System.md)

**File:** `Currency/CurrencySubsystem.h / .cpp`

Server-only `UWorldSubsystem`. The **sole external entry point** for all currency mutations. The wallet component never mutates itself — all policy, validation, rate limiting, and audit dispatch live here.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UCurrencySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // --- Primary Mutation API ---

    // Modify a single wallet by Delta (positive = gain, negative = spend).
    // Server-only. Returns Success or an error code — never silently no-ops.
    UFUNCTION(BlueprintCallable, Category = "Currency", meta = (DefaultToSelf = "Wallet"))
    EWalletMutationResult ModifyCurrency(
        UCurrencyWalletComponent* Wallet,
        FGameplayTag              CurrencyTag,
        int64                     Delta,
        TScriptInterface<ISourceIDInterface> Source,
        TScriptInterface<ISourceIDInterface> Target,
        FGuid                     SessionId = FGuid()
    );

    // Transfer Amount from one wallet to another atomically.
    // Both wallets mutate or neither does. Server-only.
    UFUNCTION(BlueprintCallable, Category = "Currency")
    EWalletMutationResult TransferCurrency(
        UCurrencyWalletComponent* From,
        UCurrencyWalletComponent* To,
        FGameplayTag              CurrencyTag,
        int64                     Amount,
        TScriptInterface<ISourceIDInterface> Source,
        TScriptInterface<ISourceIDInterface> Target,
        FGuid                     SessionId = FGuid()
    );

    // --- Rate Limiting (anti-exploit) ---

    // Max ModifyCurrency calls allowed per actor per second.
    // Configurable via project settings or console var.
    // Excess calls return EWalletMutationResult::InvalidWallet and log a warning.
    int32 MutationRateLimit = 60;

private:
    EWalletMutationResult ValidateWallet(
        const UCurrencyWalletComponent* Wallet,
        FGameplayTag CurrencyTag,
        const FCurrencySlotConfig*& OutConfig
    ) const;

    void DispatchAudit(
        FGameplayTag              EventTag,
        UCurrencyWalletComponent* PrimaryWallet,
        FGameplayTag              CurrencyTag,
        int64                     Delta,
        int64                     ResultingAmount,
        TScriptInterface<ISourceIDInterface> Source,
        TScriptInterface<ISourceIDInterface> Target,
        FGuid                     SessionId
    );

    // Per-actor mutation timestamp tracking for rate limiting.
    TMap<TWeakObjectPtr<AActor>, TArray<double>> MutationTimestamps;

    bool IsRateLimited(AActor* OwnerActor);
};
```

---

## ShouldCreateSubsystem

```cpp
bool UCurrencySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // Only create on the server (or standalone). Never on clients.
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->GetNetMode() != NM_Client);
}
```

---

## ModifyCurrency

```cpp
EWalletMutationResult UCurrencySubsystem::ModifyCurrency(
    UCurrencyWalletComponent* Wallet,
    FGameplayTag              CurrencyTag,
    int64                     Delta,
    TScriptInterface<ISourceIDInterface> Source,
    TScriptInterface<ISourceIDInterface> Target,
    FGuid                     SessionId)
{
    // 1. Validate wallet and config
    const FCurrencySlotConfig* Config = nullptr;
    EWalletMutationResult ValidationResult = ValidateWallet(Wallet, CurrencyTag, Config);
    if (ValidationResult != EWalletMutationResult::Success)
        return ValidationResult;

    // 2. Rate limit check
    if (IsRateLimited(Wallet->GetOwner()))
    {
        UE_LOG(LogCurrency, Warning, TEXT("ModifyCurrency: rate limit exceeded for %s"),
            *Wallet->GetOwner()->GetName());
        return EWalletMutationResult::InvalidWallet;
    }

    // 3. Compute new amount and clamp check
    FCurrencyLedgerEntry* Entry = Wallet->GetOrCreateEntry(CurrencyTag);
    const int64 OldAmount = Entry->Amount;
    const int64 NewAmount = OldAmount + Delta;

    if (NewAmount < Config->Min)
        return (Delta < 0)
            ? EWalletMutationResult::InsufficientFunds
            : EWalletMutationResult::ClampViolation;

    if (NewAmount > Config->Max)
        return EWalletMutationResult::ClampViolation;

    // 4. Apply mutation
    Entry->Amount = NewAmount;
    Wallet->Ledger.MarkItemDirty(*Entry);

    // 5. Notify
    Wallet->OnCurrencyChanged.Broadcast(CurrencyTag, OldAmount, NewAmount);

    // 6. Audit
    DispatchAudit(TAG_Audit_Currency_Modify, Wallet, CurrencyTag,
        Delta, NewAmount, Source, Target, SessionId);

    return EWalletMutationResult::Success;
}
```

---

## TransferCurrency

```cpp
EWalletMutationResult UCurrencySubsystem::TransferCurrency(
    UCurrencyWalletComponent* From,
    UCurrencyWalletComponent* To,
    FGameplayTag              CurrencyTag,
    int64                     Amount,
    TScriptInterface<ISourceIDInterface> Source,
    TScriptInterface<ISourceIDInterface> Target,
    FGuid                     SessionId)
{
    if (Amount <= 0)
        return EWalletMutationResult::ClampViolation;

    // 1. Validate both wallets independently
    const FCurrencySlotConfig* FromConfig = nullptr;
    EWalletMutationResult FromResult = ValidateWallet(From, CurrencyTag, FromConfig);
    if (FromResult != EWalletMutationResult::Success)
        return FromResult;

    const FCurrencySlotConfig* ToConfig = nullptr;
    EWalletMutationResult ToResult = ValidateWallet(To, CurrencyTag, ToConfig);
    if (ToResult != EWalletMutationResult::Success)
        return ToResult;

    // 2. Pre-flight checks — both must pass before any mutation
    FCurrencyLedgerEntry* FromEntry = From->GetOrCreateEntry(CurrencyTag);
    FCurrencyLedgerEntry* ToEntry   = To->GetOrCreateEntry(CurrencyTag);

    if (FromEntry->Amount - Amount < FromConfig->Min)
        return EWalletMutationResult::InsufficientFunds;

    if (ToEntry->Amount + Amount > ToConfig->Max)
        return EWalletMutationResult::RecipientClampViolation;

    // 3. Atomic apply — both sides mutate
    const int64 FromOld = FromEntry->Amount;
    const int64 ToOld   = ToEntry->Amount;

    FromEntry->Amount -= Amount;
    ToEntry->Amount   += Amount;

    From->Ledger.MarkItemDirty(*FromEntry);
    To->Ledger.MarkItemDirty(*ToEntry);

    // 4. Notify both
    From->OnCurrencyChanged.Broadcast(CurrencyTag, FromOld, FromEntry->Amount);
    To->OnCurrencyChanged.Broadcast(CurrencyTag, ToOld, ToEntry->Amount);

    // 5. Audit — single transactional group for both sides
    // Begin event
    DispatchAudit(TAG_Audit_Currency_Transfer, From, CurrencyTag,
        -Amount, FromEntry->Amount, Source, Target, SessionId);
    // Commit event (same SessionId links them for recovery queries)
    DispatchAudit(TAG_Audit_Currency_TransferCommit, To, CurrencyTag,
        Amount, ToEntry->Amount, Source, Target, SessionId);

    return EWalletMutationResult::Success;
}
```

---

## ValidateWallet

```cpp
EWalletMutationResult UCurrencySubsystem::ValidateWallet(
    const UCurrencyWalletComponent* Wallet,
    FGameplayTag CurrencyTag,
    const FCurrencySlotConfig*& OutConfig) const
{
    if (!Wallet || !Wallet->GetOwner() || !Wallet->GetOwner()->HasAuthority())
        return EWalletMutationResult::InvalidWallet;

    if (!Wallet->Definition)
        return EWalletMutationResult::InvalidWallet;

    OutConfig = Wallet->Definition->FindSlotConfig(CurrencyTag);
    if (!OutConfig)
        return EWalletMutationResult::CurrencyNotConfigured;

    return EWalletMutationResult::Success;
}
```

---

## DispatchAudit

```cpp
void UCurrencySubsystem::DispatchAudit(
    FGameplayTag              EventTag,
    UCurrencyWalletComponent* PrimaryWallet,
    FGameplayTag              CurrencyTag,
    int64                     Delta,
    int64                     ResultingAmount,
    TScriptInterface<ISourceIDInterface> Source,
    TScriptInterface<ISourceIDInterface> Target,
    FGuid                     SessionId)
{
    FAuditEntry Entry;
    Entry.EventTag         = EventTag;
    Entry.SchemaVersion    = 1;
    Entry.SessionId        = SessionId;

    // Source identity
    if (Source.GetObject())
    {
        Entry.ActorId          = Source->GetSourceGuid();  // Assumes ISourceIDInterface exposes GUID
        Entry.ActorDisplayName = Source->GetSourceDisplayName().ToString();
    }

    // Target identity in SubjectId/SubjectTag
    if (Target.GetObject())
    {
        Entry.SubjectId  = Target->GetSourceGuid();
        Entry.SubjectTag = Target->GetSourceTag();
    }

    Entry.Payload = FAuditPayloadBuilder{}
        .SetTag  (TEXT("currency"),          CurrencyTag)
        .SetInt  (TEXT("delta"),             Delta)
        .SetInt  (TEXT("resulting_amount"),  ResultingAmount)
        .SetString(TEXT("wallet_owner"),     PrimaryWallet->GetOwner()->GetName())
        .ToString();

    FGameCoreBackend::Audit(Entry);
}
```

---

## Rate Limiting

Rate limiting is a defense-in-depth measure. A legitimate game flow will never hit 60 mutations/second per actor. If this threshold fires, it is an exploit signal and must be logged and investigated.

```cpp
bool UCurrencySubsystem::IsRateLimited(AActor* OwnerActor)
{
    const double Now = FPlatformTime::Seconds();
    TArray<double>& Timestamps = MutationTimestamps.FindOrAdd(OwnerActor);

    // Evict entries older than 1 second
    Timestamps.RemoveAll([Now](double T) { return (Now - T) > 1.0; });

    if (Timestamps.Num() >= MutationRateLimit)
        return true;

    Timestamps.Add(Now);
    return false;
}
```

**Note:** `MutationTimestamps` uses `TWeakObjectPtr` keys to avoid holding strong references to destroyed actors. Periodically flush stale weak pointers during low-activity frames if the map grows large in long-running sessions.

---

## Anti-Cheat Notes

- Clients **never call** `ModifyCurrency` or `TransferCurrency` directly. Game RPCs that trigger currency mutations validate on the server before calling the subsystem.
- The subsystem does not accept RPCs. It is called only by trusted server-side code.
- All mutation results that are not `Success` should be logged at `Warning` level with actor identity for CS investigation.
- `InsufficientFunds` responses should be tracked — repeated occurrences from the same actor may indicate a client-side exploit attempt to spend funds the server knows are absent.
