// Copyright GameCore Plugin. All Rights Reserved.
#include "Journal/JournalComponent.h"
#include "Journal/JournalRegistrySubsystem.h"
#include "Persistence/PersistenceRegistrationComponent.h"
#include "EventBus/GameCoreEventBus.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY(LogJournal);

// Gameplay tag for the journal entry added event channel.
// Tag must be registered in DefaultGameplayTags.ini:
//   GameCoreEvent.Journal.EntryAdded
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GameCoreEvent_Journal_EntryAdded,
    "GameCoreEvent.Journal.EntryAdded");

UJournalComponent::UJournalComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

// ============================================================================
// Server API
// ============================================================================

void UJournalComponent::AddEntry(
    FGameplayTag EntryTag, FGameplayTag TrackTag, bool bAllowDuplicates)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

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

    // Notify server-side listeners (achievement system, etc.).
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

bool UJournalComponent::HasEntry(FGameplayTag EntryTag) const
{
    return AcquiredSet.Contains(EntryTag);
}

// ============================================================================
// Client Query API
// ============================================================================

TArray<FJournalEntryHandle> UJournalComponent::GetPage(
    FGameplayTag TrackFilter,
    FGameplayTag CollectionFilter,
    int32 PageIndex,
    int32 PageSize) const
{
    TArray<FJournalEntryHandle> Filtered = GetFiltered(TrackFilter, CollectionFilter);

    if (PageSize <= 0) return {};
    int32 StartIndex = PageIndex * PageSize;
    if (StartIndex >= Filtered.Num()) return {};
    int32 Count = FMath::Min(PageSize, Filtered.Num() - StartIndex);
    return TArray<FJournalEntryHandle>(Filtered.GetData() + StartIndex, Count);
}

int32 UJournalComponent::GetFilteredCount(
    FGameplayTag TrackFilter, FGameplayTag CollectionFilter) const
{
    return GetFiltered(TrackFilter, CollectionFilter).Num();
}

bool UJournalComponent::Client_HasEntry(FGameplayTag EntryTag) const
{
    return ClientAcquiredSet.Contains(EntryTag);
}

// ============================================================================
// IPersistableComponent
// ============================================================================

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

    // Send to owning client, then let LoadedEntries go out of scope — server frees it.
    Client_InitialJournalSync(LoadedEntries);
}

void UJournalComponent::Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion)
{
    // V1 is the initial version. No migration needed yet.
    // Future schema changes must be handled here.
}

void UJournalComponent::ClearIfSaved(uint32 FlushedGeneration)
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
    // DirtyGeneration > FlushedGeneration: a newer dirty occurred during flush — stay dirty.
}

// ============================================================================
// RPCs
// ============================================================================

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

void UJournalComponent::Client_AddEntry_Implementation(FJournalEntryHandle NewHandle)
{
    Entries.Add(NewHandle);
    ClientAcquiredSet.Add(NewHandle.EntryTag);
    OnEntryAdded.Broadcast(NewHandle);
}

// ============================================================================
// Private Helpers
// ============================================================================

void UJournalComponent::NotifyDirty()
{
    if (bDirty) return;

    if (!CachedRegComp.IsValid())
    {
        if (GetOwner())
            CachedRegComp = GetOwner()
                ->FindComponentByClass<UPersistenceRegistrationComponent>();
    }

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
            GetOwner() ? *GetOwner()->GetName() : TEXT("(null)"));
    }
#endif
}

TArray<FJournalEntryHandle> UJournalComponent::GetFiltered(
    FGameplayTag TrackFilter, FGameplayTag CollectionFilter) const
{
    // Resolve collection member set if filtering by collection.
    TSet<FGameplayTag> CollectionTags;
    bool bFilterByCollection = CollectionFilter.IsValid();
    if (bFilterByCollection)
    {
        if (UWorld* World = GetWorld())
        {
            if (UGameInstance* GI = World->GetGameInstance())
            {
                if (UJournalRegistrySubsystem* Registry =
                        GI->GetSubsystem<UJournalRegistrySubsystem>())
                {
                    CollectionTags = Registry->GetCollectionMemberTags(CollectionFilter);
                }
            }
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
