// Copyright GameCore Plugin. All Rights Reserved.
#include "Currency/CurrencyWalletComponent.h"
#include "Net/UnrealNetwork.h"
#include "Core/Backend/GameCoreBackend.h"
#include "Persistence/PersistenceRegistrationComponent.h"

// =============================================================================
// FCurrencyLedgerEntry FastArray Callbacks
// =============================================================================

void FCurrencyLedgerEntry::PostReplicatedAdd(const FCurrencyLedger& InSerializer)
{
    if (InSerializer.OwningComponent)
        InSerializer.OwningComponent->OnCurrencyChanged.Broadcast(CurrencyTag, 0, Amount);
}

void FCurrencyLedgerEntry::PostReplicatedChange(const FCurrencyLedger& InSerializer)
{
    // OldValue is not stored on the entry; pass 0 as a sentinel on client.
    // Listeners that need the old value must cache it themselves.
    if (InSerializer.OwningComponent)
        InSerializer.OwningComponent->OnCurrencyChanged.Broadcast(CurrencyTag, 0, Amount);
}

void FCurrencyLedgerEntry::PreReplicatedRemove(const FCurrencyLedger& InSerializer)
{
    // Broadcast with NewAmount = 0 when a slot is removed.
    if (InSerializer.OwningComponent)
        InSerializer.OwningComponent->OnCurrencyChanged.Broadcast(CurrencyTag, Amount, 0);
}

// =============================================================================
// UCurrencyWalletComponent
// =============================================================================

UCurrencyWalletComponent::UCurrencyWalletComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = false;
}

// =============================================================================
// Lifecycle
// =============================================================================

void UCurrencyWalletComponent::BeginPlay()
{
    Super::BeginPlay();

    // Only the server initializes state. Clients receive it via replication.
    if (!GetOwner()->HasAuthority())
        return;

    if (!Definition)
    {
        FGameCoreBackend::GetLogging(FGameplayTag{}).LogError(
            FString::Printf(TEXT("CurrencyWallet: %s has no Definition assigned."),
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

void UCurrencyWalletComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // Default: replicate to owning client only.
    // Subclass and override for guild/bank/trade wallets requiring different conditions.
    DOREPLIFETIME_CONDITION(UCurrencyWalletComponent, Ledger, COND_OwnerOnly);
}

// =============================================================================
// Read API
// =============================================================================

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

// =============================================================================
// Private Helpers
// =============================================================================

FCurrencyLedgerEntry* UCurrencyWalletComponent::GetOrCreateEntry(FGameplayTag CurrencyTag)
{
    for (FCurrencyLedgerEntry& Entry : Ledger.Items)
    {
        if (Entry.CurrencyTag == CurrencyTag)
            return &Entry;
    }
    FCurrencyLedgerEntry& New = Ledger.Items.AddDefaulted_GetRef();
    New.CurrencyTag = CurrencyTag;
    New.Amount      = 0;
    return &New;
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

// =============================================================================
// IPersistableComponent
// =============================================================================

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
