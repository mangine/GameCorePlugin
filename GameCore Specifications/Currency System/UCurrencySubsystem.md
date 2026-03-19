# UCurrencySubsystem

**Sub-page of:** [Currency System](../Currency%20System.md)

**File:** `Currency/CurrencySubsystem.h / .cpp`

Server-only `UWorldSubsystem`. The **sole external entry point** for all currency mutations. The wallet component never mutates itself — all policy, validation, and audit dispatch live here.

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

    // 2. Compute new amount and clamp check
    FCurrencyLedgerEntry* Entry = Wallet->GetOrCreateEntry(CurrencyTag);
    const int64 OldAmount = Entry->Amount;
    const int64 NewAmount = OldAmount + Delta;

    if (NewAmount < Config->Min)
        return (Delta < 0)
            ? EWalletMutationResult::InsufficientFunds
            : EWalletMutationResult::ClampViolation;

    if (NewAmount > Config->Max)
        return EWalletMutationResult::ClampViolation;

    // 3. Apply mutation
    Entry->Amount = NewAmount;
    Wallet->Ledger.MarkItemDirty(*Entry);

    // 4. Notify
    Wallet->OnCurrencyChanged.Broadcast(CurrencyTag, OldAmount, NewAmount);

    // 5. Audit
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

    // 5. Audit — two entries sharing the same SessionId link debit and credit for recovery queries
    DispatchAudit(TAG_Audit_Currency_Transfer,       From, CurrencyTag,
        -Amount, FromEntry->Amount, Source, Target, SessionId);
    DispatchAudit(TAG_Audit_Currency_TransferCommit, To,   CurrencyTag,
        Amount,  ToEntry->Amount,   Source, Target, SessionId);

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

All audit calls use `FGameCoreBackend::Audit(Entry)` — the canonical static accessor. No direct `UE_LOG`, no `GetAudit()->RecordEvent()` chain.

`ISourceIDInterface` exposes `GetSourceTag()` and `GetSourceDisplayName()` but no `FGuid`. Entity GUIDs must come from outside GameCore — the game module resolves Actor → `FGuid` via its own identity system and either subclasses `UCurrencySubsystem` or passes pre-resolved GUIDs through a higher-level API. Inside the plugin, `ActorDisplayName` and `SubjectTag` are populated from the interface; `ActorId`/`SubjectId` are left as zero GUIDs and filled by the game layer before the entry reaches the backend.

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
    Entry.EventTag      = EventTag;
    Entry.SchemaVersion = 1;
    Entry.SessionId     = SessionId;

    if (Source.GetObject())
        Entry.ActorDisplayName = Source->GetSourceDisplayName().ToString();

    if (Target.GetObject())
        Entry.SubjectTag = Target->GetSourceTag();

    Entry.Payload = FAuditPayloadBuilder{}
        .SetTag   (TEXT("currency"),         CurrencyTag)
        .SetInt   (TEXT("delta"),            Delta)
        .SetInt   (TEXT("resulting_amount"), ResultingAmount)
        .SetString(TEXT("wallet_owner"),     PrimaryWallet->GetOwner()->GetName())
        .ToString();

    FGameCoreBackend::Audit(Entry);
}
```

---

## Logging

All internal warnings and errors use `FGameCoreBackend::Log()`. No direct `UE_LOG` calls inside the subsystem.

```cpp
// Example: invalid wallet passed to ModifyCurrency
FGameCoreBackend::Log(
    ELogSeverity::Warning,
    TEXT("CurrencySubsystem"),
    FString::Printf(TEXT("ModifyCurrency: invalid wallet or no authority on actor %s"),
        *Wallet->GetOwner()->GetName()));
```

---

## Anti-Cheat Notes

- Clients **never call** `ModifyCurrency` or `TransferCurrency` directly. Game RPCs validate on the server before calling the subsystem.
- The subsystem does not accept RPCs. It is called only by trusted server-side code.
- Anti-cheat is handled via **data analysis on audit records**, not live rate limiting. Every mutation — successful or failed — produces an audit entry. Anomalies (unusual velocity, negative balance attempts, repeated `InsufficientFunds`) are detected offline against the audit log.
- All non-`Success` results should be logged via `FGameCoreBackend::Log(Warning, ...)` with actor identity so they appear in the audit-adjacent log stream.
