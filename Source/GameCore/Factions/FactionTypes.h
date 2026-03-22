// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "FactionTypes.generated.h"

class UFactionDefinition;

// ─── Log category ─────────────────────────────────────────────────────────────
GAMECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogFaction, Log, All);

// ─── Native tag namespace (handles cached at module startup) ─────────────────
namespace GameCoreEventTags
{
    GAMECORE_API extern FGameplayTag Faction_MemberJoined;
    GAMECORE_API extern FGameplayTag Faction_MemberLeft;
    GAMECORE_API extern FGameplayTag Faction_RankChanged;
}

// =============================================================================
// EFactionRelationship
// =============================================================================

/**
 * Ordered relationship scale between two factions or actors.
 * The numeric values are load-bearing: resolution uses FMath::Min on uint8 casts.
 * Do not reorder, insert, or remove values without updating all call sites.
 * Must start at 0 and be contiguous.
 */
UENUM(BlueprintType)
enum class EFactionRelationship : uint8
{
    Hostile     = 0,   // Least friendly — always wins min() resolution
    Unfriendly  = 1,
    Neutral     = 2,
    Friendly    = 3,
    Ally        = 4,   // Most friendly
};

// =============================================================================
// FFactionMembership
// =============================================================================

/**
 * A single faction membership on a UFactionComponent.
 *
 * FactionTag is always the runtime key — all subsystem lookups use it directly.
 * The Faction soft pointer is optional reference metadata; it is never loaded on the hot query path.
 *
 * Secondary memberships (bPrimary = false) participate in no relationship resolution.
 * They are grouping/affiliation tags only (crew, guild, bounty target).
 * Secondary memberships may have a null Faction asset — this is intentional.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FFactionMembership : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Soft reference to the backing UFactionDefinition.
    // May be null for secondary grouping memberships with no definition asset.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UFactionDefinition> Faction;

    // Identity tag. Must match UFactionDefinition::FactionTag when Faction is set.
    // Must be valid even when Faction is null.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag FactionTag;

    // Current rank within this faction.
    // Must be one of UFactionDefinition::RankTags when Faction is set.
    // Empty = entry rank / no rank system.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag RankTag;

    // If true, participates in relationship resolution.
    // If false, this is a secondary/grouping membership — never checked in GetRelationshipTo.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bPrimary = true;
};

// =============================================================================
// FFactionMembershipArray
// =============================================================================

/**
 * Fast-array container for FFactionMembership replication.
 * Only changed items are sent per cycle (delta-compressed).
 */
USTRUCT()
struct GAMECORE_API FFactionMembershipArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FFactionMembership> Items;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<
            FFactionMembership, FFactionMembershipArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FFactionMembershipArray>
    : public TStructOpsTypeTraitsBase2<FFactionMembershipArray>
{
    enum { WithNetDeltaSerializer = true };
};

// =============================================================================
// FFactionRelationshipOverride
// =============================================================================

USTRUCT(BlueprintType)
struct GAMECORE_API FFactionRelationshipOverride
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag FactionA;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag FactionB;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EFactionRelationship Relationship = EFactionRelationship::Neutral;
};

// =============================================================================
// FFactionSortedPair
// =============================================================================

/**
 * Order-independent key for the relationship cache.
 * Construction sorts by FName::LexicalLess — stable, deterministic, allocation-free.
 * (A, B) and (B, A) produce identical keys.
 */
USTRUCT()
struct FFactionSortedPair
{
    GENERATED_BODY()

    FGameplayTag A; // Lexicographically lesser tag
    FGameplayTag B; // Lexicographically greater or equal tag

    FFactionSortedPair() = default;

    FFactionSortedPair(FGameplayTag X, FGameplayTag Y)
    {
        if (X.GetTagName().LexicalLess(Y.GetTagName()))
            { A = X; B = Y; }
        else
            { A = Y; B = X; }
    }

    bool operator==(const FFactionSortedPair& Other) const
    {
        return A == Other.A && B == Other.B;
    }
};

inline uint32 GetTypeHash(const FFactionSortedPair& Pair)
{
    return HashCombine(GetTypeHash(Pair.A), GetTypeHash(Pair.B));
}

// =============================================================================
// FFactionMembershipChangedMessage
// =============================================================================

/**
 * Broadcast payload for Faction.MemberJoined, Faction.MemberLeft, Faction.RankChanged.
 *
 * NewRankTag is valid only for MemberJoined (if a rank was set) and RankChanged.
 * It is empty for MemberLeft.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FFactionMembershipChangedMessage
{
    GENERATED_BODY()

    // The actor whose membership changed.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> Actor = nullptr;

    // Faction that was joined, left, or whose rank changed.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag FactionTag;

    // Valid for MemberJoined and RankChanged. Empty for MemberLeft.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag NewRankTag;
};
