// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "CurrencyTypes.generated.h"

class UCurrencyWalletComponent;

// =============================================================================
// EWalletMutationResult
// =============================================================================

/**
 * Returned by every mutation on UCurrencySubsystem.
 * Callers must inspect this — never assume success.
 */
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

// =============================================================================
// FCurrencySlotConfig
// =============================================================================

/**
 * Per-currency configuration stored inside UCurrencyWalletDefinition::Slots.
 * Defines the legal value range and the starting amount.
 */
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

// =============================================================================
// FCurrencyLedgerEntry (FastArray Element)
// =============================================================================

/**
 * The replicated runtime state for one currency slot on a wallet component.
 */
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

// =============================================================================
// FCurrencyLedger (FastArray Container)
// =============================================================================

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
