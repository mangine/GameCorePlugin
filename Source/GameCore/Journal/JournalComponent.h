// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Persistence/PersistableComponent.h"
#include "Journal/JournalTypes.h"
#include "JournalComponent.generated.h"

class UPersistenceRegistrationComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogJournal, Log, All);

/**
 * UJournalComponent
 *
 * Persistent, server-authoritative journal tracker. Lives on APlayerState.
 * Implements IPersistableComponent for save/load integration.
 *
 * Server:  AcquiredSet (O(1) duplicate prevention) + ServerPersistenceBuffer (serialization)
 * Client:  Entries (full ordered history) + ClientAcquiredSet (O(1) has-entry queries)
 *
 * Mutations are server-authoritative. AddEntry is BlueprintAuthorityOnly.
 * Client data is read-only, populated by login sync RPC and incremental RPCs.
 */
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UJournalComponent
    : public UActorComponent
    , public IPersistableComponent
{
    GENERATED_BODY()

public:
    UJournalComponent();

    // =========================================================================
    // Server API
    // =========================================================================

    /**
     * Add an entry on the server.
     * bAllowDuplicates=true  → repeating entries (daily quests, replay events)
     * bAllowDuplicates=false → one-time entries (books, places, story beats)
     * No-ops if tags are invalid or entry already acquired (when !bAllowDuplicates).
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Journal")
    void AddEntry(FGameplayTag EntryTag, FGameplayTag TrackTag,
                  bool bAllowDuplicates = false);

    /**
     * Server-only. True if EntryTag has been acquired at least once.
     * O(1) via AcquiredSet.
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Journal")
    bool HasEntry(FGameplayTag EntryTag) const;

    // =========================================================================
    // Client Query API — call only on owning client
    // =========================================================================

    /**
     * Returns a sorted, filtered page of handles.
     * TrackFilter      — pass unset FGameplayTag to skip track filtering.
     * CollectionFilter — pass unset FGameplayTag to skip collection filtering.
     * Results sorted by AcquiredTimestamp descending (newest first).
     * Requires UJournalRegistrySubsystem for collection filtering.
     */
    UFUNCTION(BlueprintCallable, Category = "Journal")
    TArray<FJournalEntryHandle> GetPage(
        FGameplayTag TrackFilter,
        FGameplayTag CollectionFilter,
        int32 PageIndex,
        int32 PageSize) const;

    /**
     * Total entry count after applying filters.
     * Used by UI to compute total page count.
     */
    UFUNCTION(BlueprintCallable, Category = "Journal")
    int32 GetFilteredCount(FGameplayTag TrackFilter, FGameplayTag CollectionFilter) const;

    /**
     * Client-side HasEntry. Scans ClientAcquiredSet — O(1).
     * Do NOT use for authority checks — use the server-only HasEntry() above.
     */
    UFUNCTION(BlueprintCallable, Category = "Journal")
    bool Client_HasEntry(FGameplayTag EntryTag) const;

    /**
     * Returns the client's acquired set for use by UJournalRegistrySubsystem
     * in collection progress queries. Call only on owning client.
     */
    const TSet<FGameplayTag>& GetClientAcquiredSet() const { return ClientAcquiredSet; }

    // =========================================================================
    // Delegates — UI binding targets
    // =========================================================================

    // Fired on the client after Client_InitialJournalSync completes.
    UPROPERTY(BlueprintAssignable, Category = "Journal")
    FOnJournalSynced OnJournalSynced;

    // Fired on the client when a new entry is added during the session.
    UPROPERTY(BlueprintAssignable, Category = "Journal")
    FOnJournalEntryAdded OnEntryAdded;

    // =========================================================================
    // IPersistableComponent
    // =========================================================================

    virtual FName   GetPersistenceKey()  const override { return TEXT("JournalComponent"); }
    virtual uint32  GetSchemaVersion()   const override { return 1; }
    virtual void    Serialize_Save(FArchive& Ar) override;
    virtual void    Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void    Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion) override;
    virtual void    ClearIfSaved(uint32 FlushedGeneration) override;
    virtual bool    IsDirty() const override { return bDirty; }

    // Required dirty-tracking state (IPersistableComponent contract).
    // Must be declared on the implementing class — not inherited from the interface.
    bool   bDirty          = false;
    uint32 DirtyGeneration = 0;
    mutable TWeakObjectPtr<UPersistenceRegistrationComponent> CachedRegComp;

private:
    // =========================================================================
    // Server-only runtime state
    // =========================================================================

    // O(1) duplicate prevention. Rebuilt from ServerPersistenceBuffer on Serialize_Load.
    // Never persisted. Never replicated.
    TSet<FGameplayTag> AcquiredSet;

    // Write-only append buffer. The only in-memory entry history on the server.
    // Populated by AddEntry. Read by Serialize_Save. Cleared on EndPlay (logout).
    // Never sent to the client.
    TArray<FJournalEntryHandle> ServerPersistenceBuffer;

    // =========================================================================
    // Client-only runtime state
    // =========================================================================

    // Full ordered history. Populated by Client_InitialJournalSync, then updated
    // incrementally by Client_AddEntry. Never exists on the server after login sync.
    TArray<FJournalEntryHandle> Entries;

    // Rebuilt from Entries on Client_InitialJournalSync.
    // Allows O(1) Client_HasEntry lookups.
    TSet<FGameplayTag> ClientAcquiredSet;

    // =========================================================================
    // RPCs
    // =========================================================================

    // Sends full entry history to owning client at login.
    // Server empties its temporary LoadedEntries array immediately after this call.
    UFUNCTION(Client, Reliable)
    void Client_InitialJournalSync(const TArray<FJournalEntryHandle>& AllEntries);

    // Delivers a new handle to the owning client after AddEntry succeeds.
    UFUNCTION(Client, Reliable)
    void Client_AddEntry(FJournalEntryHandle NewHandle);

    // =========================================================================
    // Helpers
    // =========================================================================

    // Lazy dirty propagation. Call after any state change that must be persisted.
    void NotifyDirty();

    // Internal filter+sort+slice shared by GetPage and GetFilteredCount.
    TArray<FJournalEntryHandle> GetFiltered(
        FGameplayTag TrackFilter, FGameplayTag CollectionFilter) const;
};
