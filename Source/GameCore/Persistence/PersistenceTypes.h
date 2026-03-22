// PersistenceTypes.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PersistenceTypes.generated.h"

/**
 * Why a save was triggered. Controls bCritical and bFlushImmediately flags.
 * Set by UPersistenceSubsystem on every FEntityPersistencePayload.
 */
UENUM()
enum class ESerializationReason : uint8
{
    /** Timer-driven partial or full cycle. bCritical=false, bFlushImmediately=false. */
    Periodic,

    /** Actor moving between servers. bCritical=true, bFlushImmediately=false. */
    ZoneTransfer,

    /** Player disconnect. bCritical=true, bFlushImmediately=true. */
    Logout,

    /** Explicit gameplay trigger (e.g. picking up a rare item). Game-defined severity. */
    CriticalEvent,

    /** Server shutting down. bCritical=true, bFlushImmediately=true. */
    ServerShutdown,
};

/**
 * Whether the payload contains all component blobs or only dirty ones.
 * Transport/storage layer uses this to decide apply strategy.
 */
UENUM()
enum class EPayloadType : uint8
{
    /** Only dirty components. Must be applied on top of a prior Full payload. */
    Partial,

    /** All components. Self-contained — no prior state required. */
    Full,
};

/**
 * Binary blob for a single component's serialized state.
 * Key must never be renamed after shipping — it is the persistent identifier.
 */
USTRUCT()
struct GAMECORE_API FComponentPersistenceBlob
{
    GENERATED_BODY()

    /** Matches IPersistableComponent::GetPersistenceKey(). Stable across versions. */
    FName Key;

    /** Schema version at the time of serialization. Used for migration on load. */
    uint32 Version = 0;

    /** Raw binary data written by IPersistableComponent::Serialize_Save(). */
    TArray<uint8> Data;
};

/**
 * Full persistence payload for one actor.
 * Produced by UPersistenceRegistrationComponent::BuildPayload().
 * Dispatched immediately by UPersistenceSubsystem via tag-keyed FOnPayloadReady delegates.
 * Forwarded by game module to IKeyStorageService::Set().
 */
USTRUCT()
struct GAMECORE_API FEntityPersistencePayload
{
    GENERATED_BODY()

    /** Stable actor identity. From ISourceIDInterface::GetEntityGUID(). */
    FGuid EntityId;

    /**
     * Server that produced this payload.
     * Must be set via DefaultGame.ini. Stamped into every payload for audit
     * and cross-restart deduplication. Invalid GUID means deduplication is broken.
     */
    FGuid ServerInstanceId;

    /**
     * Category tag. Determines which FOnPayloadReady delegate is fired.
     * Must be registered via UPersistenceSubsystem::RegisterPersistenceTag().
     */
    FGameplayTag PersistenceTag;

    /** Whether this payload contains all components or only dirty ones. */
    EPayloadType PayloadType = EPayloadType::Partial;

    /** Why this save was triggered. */
    ESerializationReason SaveReason = ESerializationReason::Periodic;

    /** UTC Unix timestamp at payload production time. */
    int64 Timestamp = 0;

    /**
     * Place in DB service priority lane — never dropped on overflow.
     * Set by UPersistenceSubsystem based on ESerializationReason.
     * Forwarded to IKeyStorageService::Set as bCritical.
     */
    bool bCritical = false;

    /**
     * Bypass DB write-behind queue — dispatch synchronously to backend.
     * Set by UPersistenceSubsystem based on ESerializationReason.
     * Forwarded to IKeyStorageService::Set as bFlushImmediately.
     */
    bool bFlushImmediately = false;

    /** Serialized component blobs. */
    TArray<FComponentPersistenceBlob> Components;
};
