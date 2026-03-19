# FactionTypes

**Sub-page of:** [Faction System](../Faction%20System.md)

All enums, lightweight structs, and hash utilities for the Faction System. Defined in `Factions/FactionTypes.h`. This header is included by nearly every other file in the system — keep it free of `UObject` dependencies.

---

## `EFactionRelationship`

```cpp
UENUM(BlueprintType)
enum class EFactionRelationship : uint8
{
    Hostile     = 0,   // Least friendly — always wins min() resolution
    Unfriendly  = 1,
    Neutral     = 2,
    Friendly    = 3,
    Ally        = 4,   // Most friendly
};
```

**The numeric ordering is load-bearing.** Default resolution casts both values to `uint8` and calls `FMath::Min`. Do not reorder or insert values without updating all call sites. The enum must start at 0 and be contiguous.

---

## `FFactionMembership`

Represents a single faction entry on a `UFactionComponent`. Primary memberships participate in relationship resolution. Secondary memberships are grouping/affiliation tags only.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FFactionMembership : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // The faction this entry represents. May be null for secondary grouping memberships
    // that have no backing UFactionDefinition asset.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UFactionDefinition> Faction;

    // Identity tag — must match UFactionDefinition::FactionTag when Faction is set.
    // Must be valid even when Faction is null (bare secondary memberships use this as
    // their sole identifier).
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag FactionTag;

    // Current rank within this faction.
    // Must be one of UFactionDefinition::RankTags when Faction is set.
    // Empty = entry rank / no rank system for this faction.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag RankTag;

    // If true, this membership participates in relationship resolution.
    // If false, this is a secondary/grouping membership — never checked for
    // relationship, never included in GetHostileFactions().
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bPrimary = true;
};
```

**`FactionTag` is always the runtime key.** Even when `Faction` is set, all subsystem lookups use `FactionTag` directly. This avoids soft-pointer loads on the hot query path.

**Secondary memberships may have a null `Faction` asset.** This is intentional — lightweight grouping tags (crew, guild, bounty target) do not need a full `UFactionDefinition`.

---

## `FFactionMembershipArray`

Fast-array wrapper for replication. Defined alongside `FFactionMembership`.

```cpp
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
```

---

## `FFactionRelationshipOverride`

A single explicit faction-pair relationship. Used in both `UFactionRelationshipTable::ExplicitRelationships` (global) and `UFactionComponent::LocalOverrides` (per-entity).

```cpp
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
```

Pair order does not matter at authoring time. `UFactionSubsystem::BuildCache` sorts the pair before inserting into the `TMap`. `GetRelationshipTo` sorts before every lookup.

---

## `FFactionSortedPair`

The TMap key for the relationship cache. Enforces order-independence: `(A, B)` and `(B, A)` always produce the same key.

```cpp
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
```

**Why lexicographic sort?** `FGameplayTag::GetTagName()` returns a stable `FName`. `FName::LexicalLess` is deterministic and allocation-free. Using `FName` comparison rather than raw pointer comparison ensures the sort is stable across sessions regardless of tag registration order.

---

## `FFactionMembershipChangedMessage`

Payload for all three GMS faction events.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FFactionMembershipChangedMessage
{
    GENERATED_BODY()

    // The actor whose membership changed (player or NPC).
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> Actor = nullptr;

    // Faction that was joined, left, or whose rank changed.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag FactionTag;

    // Valid only for MemberJoined and RankChanged events.
    // Empty for MemberLeft.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag NewRankTag;
};
```

Broadcast channels:
- `GameCoreEvent.Faction.MemberJoined` — after a successful `JoinFaction`.
- `GameCoreEvent.Faction.MemberLeft` — after `LeaveFaction`.
- `GameCoreEvent.Faction.RankChanged` — after `SetRank`.
