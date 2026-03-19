# UJournalComponent

**Sub-page of:** [Journal System](../Journal%20System.md)

**Type:** `UActorComponent`  
**Lives on:** `APlayerState`  
**Authority:** Server mutates. Owning client receives data via login sync RPC + incremental RPCs.

---

## Responsibilities

- Server: authoritative `AddEntry` with duplicate prevention via `TSet<FGameplayTag>`
- Server: serialize/deserialize full entry history via `IPersistableComponent`
- Server: dispatch full history to owning client on login, then discard from RAM
- Server: send incremental `ClientRPC` per new entry during the session
- Client: hold full history in `TArray<FJournalEntryHandle>` for local pagination
- Client: expose `GetPage()` and `HasEntry()` query API to UI

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
    // Server API
    // -------------------------------------------------------------------------

    // Add an entry on the server. bAllowDuplicates=true for repeating entries
    // (daily quests). bAllowDuplicates=false for books, places, one-time events.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Journal")
    void AddEntry(FGameplayTag EntryTag, FGameplayTag TrackTag,
                  bool bAllowDuplicates = false);

    // Server-only. Returns true if EntryTag has been acquired at least once.
    // Uses AcquiredSet for O(1) lookup.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Journal")
    bool HasEntry(FGameplayTag EntryTag) const;

    // -------------------------------------------------------------------------
    // Client Query API — call only on owning client
    // -------------------------------------------------------------------------

    // Returns a page of handles filtered by TrackTag and/or CollectionTag.
    // Pass an unset FGameplayTag to skip that filter.
    // CollectionFilter requires UJournalRegistrySubsystem to resolve member sets.
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

    // Client-side HasEntry. Linear scan of Entries — acceptable for UI use.
    // For server authority checks, use the server-only HasEntry() above.
    UFUNCTION(BlueprintCallable, Category="Journal")
    bool Client_HasEntry(FGameplayTag EntryTag) const;

    // -------------------------------------------------------------------------
    // Delegates — UI binding targets
    // -------------------------------------------------------------------------

    // Fired on the client after Client_InitialJournalSync completes.
    UPROPERTY(BlueprintAssignable, Category="Journal")
    FOnJournalSynced OnJournalSynced;
    // Signature: ()

    // Fired on the client when a new entry is added during the session.
    UPROPERTY(BlueprintAssignable, Category="Journal")
    FOnJournalEntryAdded OnEntryAdded;
    // Signature: (FJournalEntryHandle NewHandle)

    // -------------------------------------------------------------------------
    // IPersistableComponent
    // -------------------------------------------------------------------------

    virtual void SerializeForSave(FArchive& Ar) override;
    virtual void DeserializeFromSave(FArchive& Ar) override;
    virtual FName GetPersistenceKey() const override { return TEXT("JournalComponent"); }
    virtual uint32 GetSchemaVersion() const override { return 1; }
    virtual void Migrate(FArchive& Ar, uint32 FromVersion) override;

private:
    // -------------------------------------------------------------------------
    // Server-only runtime state
    // -------------------------------------------------------------------------

    // O(1) duplicate prevention. Rebuilt from Entries on DeserializeFromSave.
    // Never persisted. Never replicated.
    TSet<FGameplayTag> AcquiredSet;

    // -------------------------------------------------------------------------
    // Client-only runtime state
    // -------------------------------------------------------------------------

    // Full history. Populated by Client_InitialJournalSync, then updated
    // incrementally by Client_AddEntry. Never exists on the server after login sync.
    TArray<FJournalEntryHandle> Entries;

    // Rebuilt from Entries on Client_InitialJournalSync and Client_AddEntry.
    // Allows O(1) Client_HasEntry lookups.
    TSet<FGameplayTag> ClientAcquiredSet;

    // -------------------------------------------------------------------------
    // RPCs
    // -------------------------------------------------------------------------

    // Called by DeserializeFromSave. Sends full history to owning client.
    // Server empties its local TArray immediately after this call.
    UFUNCTION(Client, Reliable)
    void Client_InitialJournalSync(const TArray<FJournalEntryHandle>& AllEntries);

    // Called by AddEntry after the entry is persisted.
    // Delivers the new handle to the owning client.
    UFUNCTION(Client, Reliable)
    void Client_AddEntry(FJournalEntryHandle NewHandle);
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
    if (!EntryTag.IsValid() || !TrackTag.IsValid()) return;

    if (!bAllowDuplicates && AcquiredSet.Contains(EntryTag)) return;

    FJournalEntryHandle Handle;
    Handle.EntryTag          = EntryTag;
    Handle.TrackTag          = TrackTag;
    Handle.AcquiredTimestamp = FDateTime::UtcNow().ToUnixTimestamp();

    // AcquiredSet.Add is idempotent — safe even for repeatable entries.
    // For repeatable entries it simply stays "has been acquired at least once".
    AcquiredSet.Add(EntryTag);

    // Persist and notify client.
    NotifyDirty();
    Client_AddEntry(Handle);

    // Broadcast GMS event for other systems (achievement, UI notification, etc.)
    // See GMS Events page.
    BroadcastEntryAdded(Handle);
}
```

> **Note:** The entry handle is constructed on the server and sent to the client via RPC. The server does **not** keep it in a `TArray` after this point. Persistence writes it into the binary blob via `NotifyDirty()` → next save cycle.

### `SerializeForSave` / `DeserializeFromSave`

```cpp
void UJournalComponent::SerializeForSave(FArchive& Ar)
{
    // Serialize all handles the server has ever accepted.
    // Ar is a write archive here (saving).
    //
    // IMPORTANT: The server does NOT hold Entries in RAM during the session.
    // The persistence system snapshots incrementally. Each AddEntry call
    // triggers NotifyDirty(). The serialization system calls SerializeForSave
    // which must produce the FULL payload. This requires the component to
    // maintain a write-only append buffer on the server:
    //   TArray<FJournalEntryHandle> ServerPersistenceBuffer;
    // This buffer is never sent to the client and is the only in-memory
    // history on the server. It is populated by AddEntry and cleared only
    // at session end.

    int32 Count = ServerPersistenceBuffer.Num();
    Ar << Count;
    for (FJournalEntryHandle& Handle : ServerPersistenceBuffer)
    {
        Ar << Handle.EntryTag;
        Ar << Handle.TrackTag;
        Ar << Handle.AcquiredTimestamp;
    }
}

void UJournalComponent::DeserializeFromSave(FArchive& Ar)
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

        AcquiredSet.Add(Handle.EntryTag);
        ServerPersistenceBuffer.Add(Handle); // for future SerializeForSave calls
        LoadedEntries.Add(Handle);
    }

    // Send to client, then drop. Server no longer holds LoadedEntries.
    Client_InitialJournalSync(LoadedEntries);
}
```

> **`ServerPersistenceBuffer`** is the key clarification: the server must maintain a write-only append buffer to satisfy `SerializeForSave`. It is not a history for querying — it is purely a serialization scratch pad. It grows as new entries are added during the session and is cleared when the component is destroyed (player logout).

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
    if (bFilterByCollection)
    {
        auto* Registry = GetWorld()->GetGameInstance()
            ->GetSubsystem<UJournalRegistrySubsystem>();
        if (Registry)
            CollectionTags = Registry->GetCollectionMemberTags(CollectionFilter);
    }

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
    int32 StartIndex = PageIndex * PageSize;
    if (StartIndex >= Filtered.Num()) return {};
    int32 Count = FMath::Min(PageSize, Filtered.Num() - StartIndex);
    return TArray<FJournalEntryHandle>(Filtered.GetData() + StartIndex, Count);
}
```

### `Client_InitialJournalSync` (RPC impl)

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

### `Client_AddEntry` (RPC impl)

```cpp
void UJournalComponent::Client_AddEntry_Implementation(
    FJournalEntryHandle NewHandle)
{
    Entries.Add(NewHandle);
    ClientAcquiredSet.Add(NewHandle.EntryTag);
    OnEntryAdded.Broadcast(NewHandle);
}
```

---

## Server RAM Summary

| Data | Lives on server | Size |
|---|---|---|
| `AcquiredSet` | Always (session duration) | ~8 bytes × entry count |
| `ServerPersistenceBuffer` | Always (session duration) | ~24 bytes × entry count |
| `LoadedEntries` (in `DeserializeFromSave`) | Temporary — discarded after `Client_InitialJournalSync` | 0 after login completes |

For 1000 players × 1000 entries: `AcquiredSet` ≈ 8 MB, `ServerPersistenceBuffer` ≈ 24 MB. Both are acceptable for a dedicated server. The history array (`TArray` on the client) never lives in server RAM after login.
