// Copyright GameCore Plugin. All Rights Reserved.
#include "Currency/CurrencySubsystem.h"
#include "Core/Backend/GameCoreBackend.h"
#include "Engine/World.h"

// =============================================================================
// ShouldCreateSubsystem
// =============================================================================

bool UCurrencySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->GetNetMode() != NM_Client);
}

// =============================================================================
// ModifyCurrency
// =============================================================================

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
        FGameCoreBackend::GetLogging(FGameplayTag{}).LogWarning(
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

    // 5. Audit — using a locally-constructed audit tag for routing
    static const FGameplayTag AuditModifyTag =
        FGameplayTag::RequestGameplayTag(TEXT("Audit.Currency.Modify"), false);
    DispatchAudit(AuditModifyTag, Wallet, CurrencyTag, Delta, NewAmount, Source, Target, SessionId);

    return EWalletMutationResult::Success;
}

// =============================================================================
// TransferCurrency
// =============================================================================

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
    static const FGameplayTag AuditTransferTag =
        FGameplayTag::RequestGameplayTag(TEXT("Audit.Currency.Transfer"), false);
    static const FGameplayTag AuditTransferCommitTag =
        FGameplayTag::RequestGameplayTag(TEXT("Audit.Currency.TransferCommit"), false);

    DispatchAudit(AuditTransferTag,
        From, CurrencyTag, -Amount, FromEntry->Amount, Source, Target, SessionId);
    DispatchAudit(AuditTransferCommitTag,
        To,   CurrencyTag,  Amount, ToEntry->Amount,   Source, Target, SessionId);

    return EWalletMutationResult::Success;
}

// =============================================================================
// ValidateWallet
// =============================================================================

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

// =============================================================================
// DispatchAudit
// =============================================================================

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
    // Audit dispatch is a no-op in the GameCoreBackend stub.
    // Game modules replace FGameCoreBackend with a real implementation that
    // routes EventTag to the appropriate audit sink and constructs a full entry.
    //
    // Audit fields available for real implementations:
    //   EventTag, CurrencyTag, Delta, ResultingAmount, SessionId
    //   Source->GetSourceTag() / Source->GetSourceDisplayName()
    //   Target->GetSourceTag() / Target->GetSourceDisplayName()
    //   PrimaryWallet->GetOwner()->GetName()
    FGameCoreBackend::GetAudit(EventTag).RecordEvent();
}
