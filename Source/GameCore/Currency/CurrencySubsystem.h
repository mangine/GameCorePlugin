// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Currency/CurrencyTypes.h"
#include "Currency/CurrencyWalletComponent.h"
#include "Core/SourceID/SourceIDInterface.h"
#include "CurrencySubsystem.generated.h"

/**
 * UCurrencySubsystem
 *
 * Server-only UWorldSubsystem. The sole external entry point for all currency mutations.
 * The wallet component never mutates itself — all policy, validation, and audit dispatch
 * live here.
 *
 * Created only on the server (or standalone). Clients have no subsystem instance.
 */
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
