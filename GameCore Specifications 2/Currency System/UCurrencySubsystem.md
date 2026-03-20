# UCurrencySubsystem

**File:** `Currency/CurrencySubsystem.h / .cpp`  
**Base:** `UWorldSubsystem`

Server-only world subsystem. The **sole external entry point** for all currency mutations. The wallet component never mutates itself — all policy, validation, and audit dispatch live here.

---

## Class Declaration

```cpp
UCLASS()
class GAMECORE_API UCurrencySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

    // --- Primary Mutation API ---

    // Modify a single wallet by Delta (positive = gain, negative = spend).
    // Server-only. Returns Success or an explicit error code — never silently no-ops.
    UFUNCTION(BlueprintCallable, Category = "Currency", meta = (DefaultToSelf = "Wallet"))
    EWalletMutationResult ModifyCurrency(
        UCurrencyWalletComponent*             Wallet,
        FGameplayTag                          CurrencyTag,
        int64                                 Delta,
        TScriptInterface<ISourceIDInterface>  Source,
        TScriptInterface<ISourceIDInterface>  Target,
        FGuid                                 SessionId = FGuid()
    );

    // Transfer Amount from one wallet to another atomically.
    // Both wallets mutate or neither does. Amount must be > 0. Server-only.
    UFUNCTION(BlueprintCallable, Category = "Currency")
    EWalletMutationResult TransferCurrency(
        UCurrencyWalletComponent*             From,
        UCurrencyWalletComponent*             To,
        FGameplayTag                          CurrencyTag,
        int64                                 Amount,
        TScriptInterface<ISourceIDInterface>  Source,
        TScriptInterface<ISourceIDInterface>  Target,
        FGuid                                 SessionId = FGuid()
    );

private:
    // Validates wallet pointer, authority, Definition, and slot config presence.
    // OutConfig is only valid when Success is returned.
    EWalletMutationResult ValidateWallet(
        const UCurrencyWalletComponent* Wallet,
        FGameplayTag                    CurrencyTag,
        const FCurrencySlotConfig*&     OutConfig
    ) const;

    // Dispatches an audit entry via FGameCoreBackend::GetAudit.
    void DispatchAudit(
        FGameplayTag                          EventTag,
        UCurrencyWalletComponent*             PrimaryWallet,
        FGameplayTag                          CurrencyTag,
        int64                                 Delta,
        int64                                 ResultingAmount,
        TScriptInterface<ISourceIDInterface>  Source,
        TScriptInterface<ISourceIDInterface>  Target,
        FGuid                                 SessionId
    ) const;
};
```

---

## ShouldCreateSubsystem

```cpp
bool UCurrencySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->GetNetMode() != NM_Client);
}
```

---

## ModifyCurrency

```cpp
EWalletMutationResult UCurrencySubsystem::ModifyCurrency(
    UCurrencyWalletComponent*             Wallet,
    FGameplayTag                          CurrencyTag,
    int64                                 Delta,
    TScriptInterface<ISourceIDInterface>  Source,
    TScriptInterface<ISourceIDInterface>  Target,
    FGuid                                 SessionId)
{
    // 1. Validate wallet and resolve config
    const FCurrencySlotConfig* Config = nullptr;
    EWalletMutationResult ValidationResult = ValidateWallet(Wallet, CurrencyTag, Config);
    if (ValidationResult != EWalletMutationResult::Success)
    {
        FGameCoreBackend::GetLogging(TAG_Log_Economy)->LogWarning(
            TEXT("CurrencySubsystem"),
            FString::Printf(TEXT("ModifyCurrency failed validation (result=%d) on actor %s"),
                (int32)ValidationResult, *GetNameSafe(Wallet ? Wallet->GetOwner() : nullptr)));
        return ValidationResult;
    }

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

    // 4. Notify component (server-side listeners + triggers dirty for persistence)
    Wallet->OnCurrencyChanged.Broadcast(CurrencyTag, OldAmount, NewAmount);
    Wallet->NotifyDirty();

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
    UCurrencyWalletComponent*             From,
    UCurrencyWalletComponent*             To,
    FGameplayTag                          CurrencyTag,
    int64                                 Amount,
    TScriptInterface<ISourceIDInterface>  Source,
    TScriptInterface<ISourceIDInterface>  Target,
    FGuid                                 SessionId)
{
    if (Amount <= 0)
        return EWalletMutationResult::ClampViolation;

    // 1. Validate both wallets independently — both must pass before any mutation
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

    // 3. Atomic apply — both sides mutate together
    const int64 FromOld = FromEntry->Amount;
    const int64 ToOld   = ToEntry->Amount;

    FromEntry->Amount -= Amount;
    ToEntry->Amount   += Amount;

    From->Ledger.MarkItemDirty(*FromEntry);
    To->Ledger.MarkItemDirty(*ToEntry);

    // 4. Notify both components
    From->OnCurrencyChanged.Broadcast(CurrencyTag, FromOld, FromEntry->Amount);
    To->OnCurrencyChanged.Broadcast(CurrencyTag, ToOld, ToEntry->Amount);
    From->NotifyDirty();
    To->NotifyDirty();

    // 5. Audit — two entries sharing the same SessionId link debit and credit for crash recovery
    DispatchAudit(TAG_Audit_Currency_Transfer,
        From, CurrencyTag, -Amount, FromEntry->Amount, Source, Target, SessionId);
    DispatchAudit(TAG_Audit_Currency_TransferCommit,
        To,   CurrencyTag,  Amount, ToEntry->Amount,   Source, Target, SessionId);

    return EWalletMutationResult::Success;
}
```

---

## ValidateWallet

```cpp
EWalletMutationResult UCurrencySubsystem::ValidateWallet(
    const UCurrencyWalletComponent* Wallet,
    FGameplayTag                    CurrencyTag,
    const FCurrencySlotConfig*&     OutConfig) const
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

All audit calls use `FGameCoreBackend::GetAudit(TAG_Audit_Currency_*)`. No direct `UE_LOG`, no `GetAudit()->RecordEvent()` chain.

`ISourceIDInterface` provides `GetSourceTag()` and `GetSourceDisplayName()`. Actor GUIDs must be resolved by the game layer before reaching the subsystem — `ActorId`/`SubjectId` in the audit entry are zero GUIDs inside the plugin and filled by game-layer wrappers if needed.

```cpp
void UCurrencySubsystem::DispatchAudit(
    FGameplayTag                          EventTag,
    UCurrencyWalletComponent*             PrimaryWallet,
    FGameplayTag                          CurrencyTag,
    int64                                 Delta,
    int64                                 ResultingAmount,
    TScriptInterface<ISourceIDInterface>  Source,
    TScriptInterface<ISourceIDInterface>  Target,
    FGuid                                 SessionId) const
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
        .SetString(TEXT("wallet_owner"),     GetNameSafe(PrimaryWallet->GetOwner()))
        .ToString();

    FGameCoreBackend::GetAudit(EventTag)->RecordEvent(Entry);
}
```

---

## Anti-Cheat Notes

- Clients **never call** `ModifyCurrency` or `TransferCurrency`. Game-layer RPCs validate on the server before calling the subsystem.
- The subsystem does not accept RPCs — it is called only by trusted server-side code.
- All non-`Success` results are logged via `FGameCoreBackend::GetLogging(TAG_Log_Economy)->LogWarning(...)` with actor identity so they appear in the audit-adjacent log stream.
- Anti-cheat anomaly detection (unusual velocity, repeated `InsufficientFunds`, negative balance attempts) runs **offline** against the audit log — not as live rate limiting in this subsystem.
- Every failed mutation attempt should be auditable: even validation failures can optionally be dispatched as audit entries at the game layer for security analysis.
