// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Currency/CurrencyTypes.h"
#include "CurrencyWalletDefinition.generated.h"

/**
 * Declares which currencies a wallet holds and how each is configured.
 * One asset per wallet archetype: DA_Wallet_Player, DA_Wallet_Guild, DA_Wallet_Trade, etc.
 * Never replicated, never persisted — read-only authoring data resolved once at BeginPlay.
 */
UCLASS(BlueprintType, DisplayName = "Currency Wallet Definition")
class GAMECORE_API UCurrencyWalletDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    // Currency slots this wallet supports.
    // Key   = FGameplayTag identifying the currency (e.g. Currency.Gold).
    // Value = FCurrencySlotConfig with clamp range and initial amount.
    //
    // Mutations for tags NOT present in this map are rejected with
    // EWalletMutationResult::CurrencyNotConfigured.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    TMap<FGameplayTag, FCurrencySlotConfig> Slots;

    // Returns config for the given tag, or nullptr if not configured.
    // Called by UCurrencySubsystem during validation. Pointer is valid for the lifetime
    // of the data asset (i.e. the game session). Do not cache across asset reload.
    const FCurrencySlotConfig* FindSlotConfig(const FGameplayTag& Tag) const
    {
        return Slots.Find(Tag);
    }
};
