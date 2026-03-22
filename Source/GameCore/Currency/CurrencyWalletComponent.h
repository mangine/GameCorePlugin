// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Currency/CurrencyTypes.h"
#include "Currency/CurrencyWalletDefinition.h"
#include "Persistence/PersistableComponent.h"
#include "CurrencyWalletComponent.generated.h"

class UPersistenceRegistrationComponent;

/**
 * The runtime state owner for all currency on an Actor.
 * Attaches to any Actor that needs to hold currency: APlayerState, guild actors,
 * trade session actors, NPCs, banks.
 *
 * Pure state — never mutates itself. All mutations flow through UCurrencySubsystem.
 * Implements IPersistableComponent for persistent wallets.
 *
 * REPLICATION: Default COND_OwnerOnly. Subclass and override
 * GetLifetimeReplicatedProps for guild/bank/trade wallets requiring different conditions.
 */
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
    virtual bool    IsDirty() const override { return bDirty; }

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
