# UJournalComponent

**Files:** `GameCore/Source/GameCore/Journal/JournalComponent.h` / `.cpp`  
**Type:** `UActorComponent` + `IPersistableComponent`  
**Lives on:** `APlayerState`  
**Authority:** Server mutates. Owning client receives data via login-sync RPC + incremental RPCs.

---

## Responsibilities

- Server: `AddEntry` with duplicate prevention via `TSet<FGameplayTag>`
- Server: serialize/deserialize full entry history via `IPersistableComponent`
- Server: dispatch full history to owning client at login, then discard the temporary array
- Server: send incremental `Client_AddEntry` RPC per new entry during the session
- Server: broadcast `FJournalEntryAddedMessage` on the Event Bus after each successful add
- Client: hold full ordered history in `TArray<FJournalEntryHandle>` for local pagination
- Client: expose `GetPage()`, `GetFilteredCount()`, `HasEntry()` query API to UI

---

## Class Definition

```cpp
// JournalComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Persistence/PersistableComponent.h"  // IPersistableComponent
#include "Journal/JournalTypes.h"
#include "JournalComponent.generated.h"

UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
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
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Journal")
    void AddEntry(FGameplayTag EntryTag, FGameplayTag TrackTag,
                  bool bAllowDuplicates = false);

    /**
     * Server-only. True if EntryTag has been acquired at least once.
     * O(1) via AcquiredSet.
     */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Journal")
    bool HasEntry(FGameplayTag EntryTag) const;

    // =========================================================================
    // Client Query API — call only on owning client
    // =========================================================================

    /**
     * Returns a sorted, filtered page of handles.
     * TrackFilter      — pass unset FGameplayTag to skip track filtering
     * CollectionFilter — pass unset FGameplayTag to skip collection filtering
     * Results sorted by AcquiredTimestamp descending (newest first).
     * Requires UJournalRegistrySubsystem for collection filtering.
     */
    UFUNCTION(BlueprintCallable, Category="Journal")
    TArray<FJournalEntryHandle> GetPage(
        FGameplayTag TrackFilter,
        FGameplayTag CollectionFilter,
        int32 PageIndex,
        int32 PageSize) const;

    /**
     * Total entry count after applying filters.
     * Used by UI to compute total page count.
     */
    UFUNCTION(BlueprintCallable, Category="Journal")
    int32 GetFilteredCount(FGameplayTag TrackFilter, FGameplayTag CollectionFilter) const;

    /**
     * Client-side HasEntry. Scans ClientAcquiredSet — O(1).
     * Do NOT use for authority checks — use the server-only HasEntry() above.
     */
    UFUNCTION(BlueprintCallable, Category="Journal")
    bool Client_HasEntry(FGameplayTag EntryTag) const;

    /**
     * Returns the client's acquired set for use by UJournalRegistrySubsystem
     * in collection progress queries.
     * Call only on owning client.
     */
    const TSet<FGameplayTag>& GetClientAcquiredSet() const { return ClientAcquiredSet; }

    // =========================================================================
    // Delegates — UI binding targets
    // =========================================================================

    // Fired on the client after Client_InitialJournalSync completes.
    UPROPERTY(BlueprintAssignable, Category="Journal")
    FOnJournalSynced OnJournalSynced;

    // Fired on the client when a new entry is added during the session.
    UPROPERTY(BlueprintAssignable, Category="Journal")
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

    // Required dirty-tracking state (IPersistableComponent contract)
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
```

---

## Method Implementations

### `AddEntry`

```cpp
void UJournalComponent::AddEntry(
    FGameplayTag EntryTag, FGameplayTag TrackTag, bool bAllowDuplicates)
{
    if (!GetOwner()->HasAuthority()) return;

    if (!EntryTag.IsValid() || !TrackTag.IsValid())
    {
        UE_LOG(LogJournal, Warning,
            TEXT("AddEntry: invalid EntryTag or TrackTag — skipped."));
        return;
    }

    if (!bAllowDuplicates && AcquiredSet.Contains(EntryTag)) return;

    FJournalEntryHandle Handle;
    Handle.EntryTag          = EntryTag;
    Handle.TrackTag          = TrackTag;
    Handle.AcquiredTimestamp = FDateTime::UtcNow().ToUnixTimestamp();

    // Idempotent — for repeating entries it stays "has been acquired at least once".
    AcquiredSet.Add(EntryTag);
    ServerPersistenceBuffer.Add(Handle);

    NotifyDirty();
    Client_AddEntry(Handle);

    // Notify server-side listeners (achievement system, etc.)
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FJournalEntryAddedMessage Msg;
        Msg.PlayerState = GetOwner<APlayerState>();
        Msg.Handle      = Handle;
        Bus->Broadcast<FJournalEntryAddedMessage>(
            TAG_GameCoreEvent_Journal_EntryAdded,
            Msg,
            EGameCoreEventScope::ServerOnly);
    }
}
```

### `HasEntry` (server)

```cpp
bool UJournalComponent::HasEntry(FGameplayTag EntryTag) const
{
    return AcquiredSet.Contains(EntryTag);
}
```

### `Serialize_Save`

```cpp
void UJournalComponent::Serialize_Save(FArchive& Ar)
{
    // Ar is a write archive.
    int32 Count = ServerPersistenceBuffer.Num();
    Ar << Count;
    for (FJournalEntryHandle& Handle : ServerPersistenceBuffer)
    {
        Ar << Handle.EntryTag;
        Ar << Handle.TrackTag;
        Ar << Handle.AcquiredTimestamp;
    }
}
```

### `Serialize_Load`

```cpp
void UJournalComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    int32 Count = 0;
    Ar << Count;

    AcquiredSet.Reserve(Count);
    ServerPersistenceBuffer.Reserve(Count);
    TArray<FJournalEntryHandle> LoadedEntries;
    LoadedEntries.Reserve(Count);

    for (int32 i = 0; i < Count; ++i)
    {
        FJournalEntryHandle Handle;
        Ar << Handle.EntryTag;
        Ar << Handle.TrackTag;
        Ar << Handle.AcquiredTimestamp;

        AcquiredSet.Add(Handle.EntryTag);
        ServerPersistenceBuffer.Add(Handle);
        LoadedEntries.Add(Handle);
    }

    // Send to client, then let LoadedEntries go out of scope — server frees it.
    Client_InitialJournalSync(LoadedEntries);
}
```

> **`ServerPersistenceBuffer`** is the serialization source. It is rebuilt from the save blob on load and appended by each `AddEntry` during the session. It is the only entry history the server holds.

### `Migrate`

```cpp
void UJournalComponent::Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion)
{
    // V1 is the initial version. No migration needed yet.
    // Future schema changes must be handled here.
}
```

### `ClearIfSaved`

```cpp
void UJournalComponent::ClearIfSaved(uint32 FlushedGeneration)
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
    // DirtyGeneration > FlushedGeneration: a newer dirty occurred during flush — stay dirty.
}
```

### `NotifyDirty`

```cpp
void UJournalComponent::NotifyDirty()
{
    if (bDirty) return;
    if (!CachedRegComp.IsValid())
        CachedRegComp = GetOwner()
            ->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (CachedRegComp.IsValid())
    {
        DirtyGeneration = CachedRegComp->SaveGeneration;
        bDirty = true;
        CachedRegComp->MarkDirty();
    }
#if !UE_BUILD_SHIPPING
    else
    {
        UE_LOG(LogJournal, Warning,
            TEXT("UJournalComponent::NotifyDirty — no UPersistenceRegistrationComponent "
                 "found on actor '%s'. Journal changes will not be persisted."),
            *GetOwner()->GetName());
    }
#endif
}
```

### `GetPage` (client)

```cpp
TArray<FJournalEntryHandle> UJournalComponent::GetPage(
    FGameplayTag TrackFilter,
    FGameplayTag CollectionFilter,
    int32 PageIndex,
    int32 PageSize) const
{
    TArray<FJournalEntryHandle> Filtered = GetFiltered(TrackFilter, CollectionFilter);

    int32 StartIndex = PageIndex * PageSize;
    if (StartIndex >= Filtered.Num() || PageSize <= 0) return {};
    int32 Count = FMath::Min(PageSize, Filtered.Num() - StartIndex);
    return TArray<FJournalEntryHandle>(Filtered.GetData() + StartIndex, Count);
}
```

### `GetFilteredCount` (client)

```cpp
int32 UJournalComponent::GetFilteredCount(
    FGameplayTag TrackFilter, FGameplayTag CollectionFilter) const
{
    return GetFiltered(TrackFilter, CollectionFilter).Num();
}
```

### `GetFiltered` (private helper)

```cpp
TArray<FJournalEntryHandle> UJournalComponent::GetFiltered(
    FGameplayTag TrackFilter, FGameplayTag CollectionFilter) const
{
    // Resolve collection member set if filtering by collection.
    TSet<FGameplayTag> CollectionTags;
    bool bFilterByCollection = CollectionFilter.IsValid();
    if (bFilterByCollection)
    {
        if (auto* Registry = GetWorld()->GetGameInstance()
                ->GetSubsystem<UJournalRegistrySubsystem>())
        {
            CollectionTags = Registry->GetCollectionMemberTags(CollectionFilter);
        }
    }

    TArray<FJournalEntryHandle> Filtered;
    Filtered.Reserve(Entries.Num());
    for (const FJournalEntryHandle& Handle : Entries)
    {
        if (TrackFilter.IsValid() && Handle.TrackTag != TrackFilter) continue;
        if (bFilterByCollection && !CollectionTags.Contains(Handle.EntryTag)) continue;
        Filtered.Add(Handle);
    }

    // Sort descending by timestamp (newest first).
    Filtered.Sort([](const FJournalEntryHandle& A, const FJournalEntryHandle& B)
    {
        return A.AcquiredTimestamp > B.AcquiredTimestamp;
    });

    return Filtered;
}
```

### `Client_InitialJournalSync_Implementation`

```cpp
void UJournalComponent::Client_InitialJournalSync_Implementation(
    const TArray<FJournalEntryHandle>& AllEntries)
{
    Entries = AllEntries;
    ClientAcquiredSet.Reset();
    ClientAcquiredSet.Reserve(AllEntries.Num());
    for (const FJournalEntryHandle& Handle : AllEntries)
        ClientAcquiredSet.Add(Handle.EntryTag);

    OnJournalSynced.Broadcast();
}
```

### `Client_AddEntry_Implementation`

```cpp
void UJournalComponent::Client_AddEntry_Implementation(FJournalEntryHandle NewHandle)
{
    Entries.Add(NewHandle);
    ClientAcquiredSet.Add(NewHandle.EntryTag);
    OnEntryAdded.Broadcast(NewHandle);
}
```

### `Client_HasEntry`

```cpp
bool UJournalComponent::Client_HasEntry(FGameplayTag EntryTag) const
{
    return ClientAcquiredSet.Contains(EntryTag);
}
```

---

## Server RAM Summary

| Data | Lives on server | Size (1000 entries) |
|---|---|---|
| `AcquiredSet` | Session duration | ~8 KB |
| `ServerPersistenceBuffer` | Session duration | ~24 KB |
| `LoadedEntries` (in `Serialize_Load`) | Temporary — freed after RPC | 0 after login completes |

For 1000 concurrent players × 1000 entries: `AcquiredSet` ≈ 8 MB, `ServerPersistenceBuffer` ≈ 24 MB. Acceptable for a dedicated MMORPG server.
