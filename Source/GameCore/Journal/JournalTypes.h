// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/Texture2D.h"
#include "JournalTypes.generated.h"

class APlayerState;

// ============================================================================
// FJournalEntryHandle
//
// The atomic unit of the journal. Exactly 24 bytes:
//   FGameplayTag EntryTag          (8 bytes — FName is 8 bytes)
//   FGameplayTag TrackTag          (8 bytes)
//   int64        AcquiredTimestamp (8 bytes)
//
// Stored in ServerPersistenceBuffer (server) and Entries (client).
// Contains NO content — only identity and acquisition time.
// ============================================================================

USTRUCT(BlueprintType)
struct GAMECORE_API FJournalEntryHandle
{
    GENERATED_BODY()

    // Unique identity of this entry type. Maps to a UJournalEntryDataAsset
    // via UJournalRegistrySubsystem::GetEntryAsset(). Replicates as a
    // network index — never a string.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag EntryTag;

    // Top-level track this entry belongs to. Used for tab filtering on the client.
    // e.g. Journal.Track.Books | Journal.Track.Adventure
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag TrackTag;

    // Server UTC unix epoch (seconds) at time of acquisition.
    // Used for descending sort in GetPage().
    UPROPERTY(BlueprintReadOnly)
    int64 AcquiredTimestamp = 0;
};

// ============================================================================
// FJournalRenderedDetails
//
// Output of IJournalEntry::BuildDetails(). Contains everything the UI needs
// to render one entry's content panel. Heavy assets are already loaded when
// this struct is constructed.
// ============================================================================

USTRUCT(BlueprintType)
struct GAMECORE_API FJournalRenderedDetails
{
    GENERATED_BODY()

    // UE Rich Text markup string. Compatible with URichTextBlock + UDataTable decorators.
    UPROPERTY(BlueprintReadOnly)
    FText RichBodyText;

    // Optional header image displayed above body text.
    // Loaded by the time BuildDetails calls OnReady.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UTexture2D> HeaderImage;
};

// ============================================================================
// FJournalCollectionProgress
//
// Result of UJournalRegistrySubsystem::GetCollectionProgress().
// Always derived at runtime — never persisted per player.
// ============================================================================

USTRUCT(BlueprintType)
struct GAMECORE_API FJournalCollectionProgress
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 Found = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Total = 0;

    float Ratio() const
    {
        return Total > 0 ? static_cast<float>(Found) / static_cast<float>(Total) : 0.f;
    }

    bool IsComplete() const { return Total > 0 && Found >= Total; }
};

// ============================================================================
// FJournalEntryAddedMessage
//
// Event Bus message broadcast by UJournalComponent::AddEntry() on the server.
// Channel:  GameCoreEvent.Journal.EntryAdded
// Scope:    EGameCoreEventScope::ServerOnly
// ============================================================================

USTRUCT(BlueprintType)
struct GAMECORE_API FJournalEntryAddedMessage
{
    GENERATED_BODY()

    // The PlayerState that received the entry.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<APlayerState> PlayerState = nullptr;

    // The handle that was added.
    UPROPERTY(BlueprintReadOnly)
    FJournalEntryHandle Handle;
};

// ============================================================================
// Delegate Declarations
// ============================================================================

// Fired on the client after Client_InitialJournalSync completes.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJournalSynced);

// Fired on the client when a new entry arrives via Client_AddEntry RPC.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnJournalEntryAdded, FJournalEntryHandle, NewHandle);
