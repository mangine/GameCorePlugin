# UJournalComponent

**Files:** `GameCore/Source/GameCore/Journal/JournalComponent.h` / `.cpp`  
**Type:** `UActorComponent` + `IPersistableComponent`  
**Lives on:** `APlayerState`  
**Authority:** Server mutates. Owning client receives data via login sync RPC and incremental RPCs.

---

## Responsibilities

- Server: authoritative `AddEntry` with O(1) duplicate prevention via `TSet<FGameplayTag>`
- Server: serialize/deserialize full entry history via `IPersistableComponent`
- Server: dispatch full history to owning client on login (via `Serialize_Load`), then discard the temporary query array
- Server: send incremental `Client` RPC per new entry during the session
- Client: hold full history in `TArray<FJournalEntryHandle>` for local pagination
- Client: expose `GetPage()`, `GetFilteredCount()`, `Client_HasEntry()`, and `GetClientAcquiredSet()` query API to UI

---

## Class Definition

```cpp
// JournalComponent.h
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UJournalComponent
    : public UActorComponent
    , public IPersistableComponent
{
    GENERATED_BODY()

public:
    UJournalComponent();

    // -------------------------------------------------------------------------
    // Server API — call only with authority
    // -------------------------------------------------------------------------

    // Add an entry. bAllowDuplicates=true for repeating entries (daily quests).
    // bAllowDuplicates=false for books, places, one-time events.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Journal")
    void AddEntry(FGameplayTag EntryTag, FGameplayTag TrackTag,
                  bool bAllowDuplicates = false);

    // Returns true if EntryTag has been acquired at least once.
    // O(1) lookup via AcquiredSet.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Journal")
    bool HasEntry(FGameplayTag EntryTag) const;

    // -------------------------------------------------------------------------
    // Client Query API — call only on owning client
    // -------------------------------------------------------------------------

    // Returns a page of handles filtered by TrackTag and/or CollectionTag.
    // Pass an unset FGameplayTag to skip that filter.
    // Results are sorted by AcquiredTimestamp descending (most recent first).
    UFUNCTION(BlueprintCallable, Category="Journal")
    TArray<FJournalEntryHandle> GetPage(
        FGameplayTag TrackFilter,
        FGameplayTag CollectionFilter,
        int32 PageIndex,
        int32 PageSize) const;

    // Total count after applying filters. Used by UI to compute page count.
    UFUNCTION(BlueprintCallable, Category="Journal")
    int32 GetFilteredCount(FGameplayTag TrackFilter, FGameplayTag CollectionFilter) const;

    // Client-side HasEntry. O(1) via ClientAcquiredSet.
    // For server authority checks use the server-only HasEntry() above.
    UFUNCTION(BlueprintCallable, Category="Journal")
    bool Client_HasEntry(FGameplayTag EntryTag) const;

    // Returns the client-side acquired set. Used by collection progress queries.
    // Call only on owning client.
    const TSet<FGameplayTag>& GetClientAcquiredSet() const { return ClientAcquiredSet; }

    // -------------------------------------------------------------------------
    // Delegates — UI binding targets (client-side)
    // -------------------------------------------------------------------------

    // Fired on the client after Client_InitialJournalSync completes.
    UPROPERTY(BlueprintAssignable, Category="Journal")
    FOnJournalSynced OnJournalSynced;

    // Fired on the client when a new entry is added during the session.
    UPROPERTY(BlueprintAssignable, Category="Journal")
    FOnJournalEntryAdded OnEntryAdded;

    // -------------------------------------------------------------------------
    // IPersistableComponent
    // -------------------------------------------------------------------------

    virtual FName     GetPersistenceKey()   const override { return TEXT("JournalComponent"); }
    virtual uint32    GetSchemaVersion()    const override { return 1; }
    virtual void      Serialize_Save(FArchive& Ar) override;
    virtual void      Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void      Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion) override;
    virtual void      ClearIfSaved(uint32 FlushedGeneration) override;

    // Required dirty-tracking state (IPersistableComponent pattern)
    bool   bDirty          = false;
    uint32 DirtyGeneration = 0;
    mutable TWeakObjectPtr<UPersistenceRegistrationComponent> CachedRegComp;

private:
    void NotifyDirty();

    // -------------------------------------------------------------------------
    // Server-only runtime state
    // -------------------------------------------------------------------------

    // O(1) duplicate prevention. Rebuilt from ServerPersistenceBuffer on Serialize_Load.
    // Never persisted. Never replicated.
    TSet<FGameplayTag> AcquiredSet;

    // Write-only serialization buffer. Populated by AddEntry, read by Serialize_Save.
    // Never sent to the client directly. Never used for queries.
    TArray<FJournalEntryHandle> ServerPersistenceBuffer;

    // -------------------------------------------------------------------------
    // Client-only runtime state
    // -------------------------------------------------------------------------

    // Full history. Populated by Client_InitialJournalSync, then updated
    // incrementally by Client_AddEntry. Never exists on the server after login sync.
    TArray<FJournalEntryHandle> Entries;

    // Rebuilt from Entries on Client_InitialJournalSync and updated by Client_AddEntry.
    // Allows O(1) Client_HasEntry lookups.
    TSet<FGameplayTag> ClientAcquiredSet;

    // Cached subsystem pointer resolved at BeginPlay.
    UPROPERTY()
    TObjectPtr<UJournalRegistrySubsystem> RegistrySubsystem;

    // -------------------------------------------------------------------------
    // RPCs
    // -------------------------------------------------------------------------

    // Sends full history to owning client. Called inside Serialize_Load.
    // Server drops its temp LoadedEntries array immediately after this call.
    UFUNCTION(Client, Reliable)
    void Client_InitialJournalSync(const TArray<FJournalEntryHandle>& AllEntries);

    // Delivers a new handle to the owning client.
    UFUNCTION(Client, Reliable)
    void Client_AddEntry(FJournalEntryHandle NewHandle);
};
```

---

## Method Implementations

### `BeginPlay`

```cpp
void UJournalComponent::BeginPlay()
{
    Super::BeginPlay();
    // Cache registry subsystem for client query methods.
    if (UGameInstance* GI = GetWorld()->GetGameInstance())
        RegistrySubsystem = GI->GetSubsystem<UJournalRegistrySubsystem>();
}
```

### `AddEntry`

```cpp
void UJournalComponent::AddEntry(
    FGameplayTag EntryTag, FGameplayTag TrackTag, bool bAllowDuplicates)
{
    if (!GetOwner()->HasAuthority()) return;
    if (!EntryTag.IsValid() || !TrackTag.IsValid())
    {
        UE_LOG(LogJournal, Warning, TEXT("AddEntry: invalid tag(s). Entry=%s Track=%s"),
            *EntryTag.ToString(), *TrackTag.ToString());
        return;
    }

    if (!bAllowDuplicates && AcquiredSet.Contains(EntryTag)) return;

    FJournalEntryHandle Handle;
    Handle.EntryTag          = EntryTag;
    Handle.TrackTag          = TrackTag;
    Handle.AcquiredTimestamp = FDateTime::UtcNow().ToUnixTimestamp();

    // AcquiredSet.Add is idempotent — safe for repeatable entries.
    AcquiredSet.Add(EntryTag);
    ServerPersistenceBuffer.Add(Handle);

    NotifyDirty();
    Client_AddEntry(Handle);

    // Broadcast via Event Bus for external systems (achievements, audio, etc.)
    if (auto* Bus = UGameCoreEventBus::Get(this))
    {
        FJournalEntryAddedMessage Msg;
        Msg.PlayerState = GetOwner<APlayerState>();
        Msg.Handle      = Handle;
        Bus->Broadcast(
            TAG_GameCoreEvent_Journal_EntryAdded,
            FInstancedStruct::Make(Msg),
            EGameCoreEventScope::ServerOnly);
    }
}
```

### `Serialize_Save`

```cpp
void UJournalComponent::Serialize_Save(FArchive& Ar)
{
    // Ar is a write archive. Serialize_Save must be strictly read-only on state.
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

    TArray<FJournalEntryHandle> LoadedEntries;
    LoadedEntries.Reserve(Count);
    AcquiredSet.Reserve(Count);
    ServerPersistenceBuffer.Reserve(Count);

    for (int32 i = 0; i < Count; ++i)
    {
        FJournalEntryHandle Handle;
        Ar << Handle.EntryTag;
        Ar << Handle.TrackTag;
        Ar << Handle.AcquiredTimestamp;

        AcquiredSet.Add(Handle.EntryTag);         // rebuild dedup set
        ServerPersistenceBuffer.Add(Handle);      // rebuild serialization buffer
        LoadedEntries.Add(Handle);                // temporary — for RPC only
    }

    // Send to owning client then discard the temp array.
    // Server never holds LoadedEntries after this call.
    Client_InitialJournalSync(LoadedEntries);
}
```

### `Migrate`

```cpp
void UJournalComponent::Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion)
{
    // Schema version 1: initial. No migration needed for additive changes.
    // Override here when schema changes require data transformation.
}
```

### `ClearIfSaved` / `NotifyDirty`

```cpp
void UJournalComponent::ClearIfSaved(uint32 FlushedGeneration)
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
}

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
                 "found on %s. Entry will not be persisted."),
            *GetOwner()->GetName());
    }
#endif
}
```

### `GetPage` (client-only)

```cpp
TArray<FJournalEntryHandle> UJournalComponent::GetPage(
    FGameplayTag TrackFilter,
    FGameplayTag CollectionFilter,
    int32 PageIndex,
    int32 PageSize) const
{
    // Build collection member set if filter is active.
    TSet<FGameplayTag> CollectionTags;
    bool bFilterByCollection = CollectionFilter.IsValid();
    if (bFilterByCollection && RegistrySubsystem)
        CollectionTags = RegistrySubsystem->GetCollectionMemberTags(CollectionFilter);

    // Filter Entries.
    TArray<FJournalEntryHandle> Filtered;
    for (const FJournalEntryHandle& Handle : Entries)
    {
        if (TrackFilter.IsValid() && Handle.TrackTag != TrackFilter) continue;
        if (bFilterByCollection && !CollectionTags.Contains(Handle.EntryTag)) continue;
        Filtered.Add(Handle);
    }

    // Sort descending by timestamp.
    Filtered.Sort([](const FJournalEntryHandle& A, const FJournalEntryHandle& B)
    {
        return A.AcquiredTimestamp > B.AcquiredTimestamp;
    });

    // Slice.
    const int32 StartIndex = PageIndex * PageSize;
    if (StartIndex >= Filtered.Num()) return {};
    const int32 Count = FMath::Min(PageSize, Filtered.Num() - StartIndex);
    return TArray<FJournalEntryHandle>(Filtered.GetData() + StartIndex, Count);
}
```

### `GetFilteredCount` (client-only)

```cpp
int32 UJournalComponent::GetFilteredCount(
    FGameplayTag TrackFilter, FGameplayTag CollectionFilter) const
{
    TSet<FGameplayTag> CollectionTags;
    bool bFilterByCollection = CollectionFilter.IsValid();
    if (bFilterByCollection && RegistrySubsystem)
        CollectionTags = RegistrySubsystem->GetCollectionMemberTags(CollectionFilter);

    int32 Count = 0;
    for (const FJournalEntryHandle& Handle : Entries)
    {
        if (TrackFilter.IsValid() && Handle.TrackTag != TrackFilter) continue;
        if (bFilterByCollection && !CollectionTags.Contains(Handle.EntryTag)) continue;
        ++Count;
    }
    return Count;
}
```

### `Client_InitialJournalSync` RPC Implementation

```cpp
void UJournalComponent::Client_InitialJournalSync_Implementation(
    const TArray<FJournalEntryHandle>& AllEntries)
{
    Entries = AllEntries;
    ClientAcquiredSet.Reserve(AllEntries.Num());
    for (const FJournalEntryHandle& Handle : AllEntries)
        ClientAcquiredSet.Add(Handle.EntryTag);

    OnJournalSynced.Broadcast();
}
```

### `Client_AddEntry` RPC Implementation

```cpp
void UJournalComponent::Client_AddEntry_Implementation(FJournalEntryHandle NewHandle)
{
    Entries.Add(NewHandle);
    ClientAcquiredSet.Add(NewHandle.EntryTag);
    OnEntryAdded.Broadcast(NewHandle);
}
```

---

## Server RAM Summary

| Data | Lives on server | Size (per player, 1000 entries) |
|---|---|---|
| `AcquiredSet` | Always (session duration) | ~8 KB |
| `ServerPersistenceBuffer` | Always (session duration) | ~24 KB |
| `LoadedEntries` (in `Serialize_Load`) | Temporary — discarded after `Client_InitialJournalSync` | 0 after login completes |

For 1000 players × 1000 entries: `AcquiredSet` ≈ 8 MB, `ServerPersistenceBuffer` ≈ 24 MB. Acceptable for a dedicated server.
