# Alignment Runtime Types

**File:** `GameCore/Source/GameCore/Alignment/AlignmentTypes.h`

Covers the three runtime structs used by `UAlignmentComponent`: the batch mutation input (`FAlignmentDelta`), the per-axis live data entry (`FAlignmentEntry`), and the replicated container (`FAlignmentArray`).

---

## `FAlignmentDelta` — Batch Mutation Input

```cpp
/**
 * One axis change within a batch call to UAlignmentComponent::ApplyAlignmentDeltas.
 * Positive delta moves toward max; negative delta moves toward min.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FAlignmentDelta
{
    GENERATED_BODY()

    /** The axis to mutate. Must match a registered UAlignmentDefinition::AlignmentTag. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
    FGameplayTag AlignmentTag;

    /**
     * The amount to add to the underlying value.
     * Positive = toward max (e.g. toward "good" on a good/evil axis).
     * Negative = toward min (e.g. toward "evil").
     * Clamped to [SaturatedMin, SaturatedMax] after application.
     * A value of exactly 0 is skipped before any lookup.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
    float Delta = 0.f;
};
```

---

## `FAlignmentEntry` — Per-Axis Live Data

```cpp
/**
 * Per-axis runtime state for one player. Lives inside FAlignmentArray.
 * One entry per registered UAlignmentDefinition.
 *
 * EffectiveMin/Max are cached here at registration time so that
 * GetEffectiveAlignment works correctly on clients (which do not have
 * access to the server-only Definitions map).
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FAlignmentEntry : public FFastArraySerializerItem
{
    GENERATED_BODY()

    /** Matches UAlignmentDefinition::AlignmentTag. Used as the lookup key. */
    UPROPERTY()
    FGameplayTag AlignmentTag;

    /**
     * Raw accumulated value. Clamped to [SaturatedMin, SaturatedMax] on every mutation.
     * This is the only field persisted by IPersistableComponent.
     */
    UPROPERTY()
    float UnderlyingValue = 0.f;

    /**
     * Cached from UAlignmentDefinition at RegisterAlignment time.
     * Replicated so that clients can call GetEffectiveAlignment without
     * requiring access to the server-only Definitions map.
     */
    UPROPERTY()
    float EffectiveMin = -100.f;

    UPROPERTY()
    float EffectiveMax = 100.f;

    /** Derived at query time — never stored or replicated separately. */
    float GetEffectiveValue() const
    {
        return FMath::Clamp(UnderlyingValue, EffectiveMin, EffectiveMax);
    }
};
```

> **Why store `EffectiveMin/Max` on the entry?**
> `Definitions` is populated only on the server via `RegisterAlignment`. Clients receive `FAlignmentEntry` data via replication but have no access to `Definitions`. Caching the range on the entry costs 8 bytes per axis and eliminates the need to either replicate definitions separately or force clients to hold definition asset references.

---

## `FAlignmentArray` — Replicated Container

```cpp
/**
 * FFastArraySerializer container for per-player alignment runtime data.
 * Replicated on UAlignmentComponent (which lives on APlayerState).
 * Only dirty items are sent over the wire per update.
 */
USTRUCT()
struct GAMECORE_API FAlignmentArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FAlignmentEntry> Items;

    /** Lookup by tag. Returns nullptr if not found. O(n) — axis count is bounded and small. */
    FAlignmentEntry* FindByTag(FGameplayTag Tag)
    {
        return Items.FindByPredicate([&](const FAlignmentEntry& E)
        {
            return E.AlignmentTag == Tag;
        });
    }

    const FAlignmentEntry* FindByTag(FGameplayTag Tag) const
    {
        return Items.FindByPredicate([&](const FAlignmentEntry& E)
        {
            return E.AlignmentTag == Tag;
        });
    }

    /** Required by FFastArraySerializer. */
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FAlignmentEntry, FAlignmentArray>(
            Items, DeltaParms, *this);
    }
};

/** Required trait declaration for FFastArraySerializer. */
template<>
struct TStructOpsTypeTraits<FAlignmentArray> : public TStructOpsTypeTraitsBase2<FAlignmentArray>
{
    enum { WithNetDeltaSerializer = true };
};
```

---

## Lifecycle

1. On `RegisterAlignment`: a new `FAlignmentEntry` is appended to `Items` with `UnderlyingValue = 0` and `EffectiveMin/Max` copied from the definition. `MarkItemDirty` is called so the initial state replicates to the client.
2. On `ApplyAlignmentDeltas`: matching entries are mutated in-place and `MarkItemDirty` called for each changed entry.
3. `FFastArraySerializer` replicates only dirty items to the owning client at the next network tick.
4. On the client, `OnRep_AlignmentData` fires → `OnAlignmentDataReplicated` delegate broadcasts.
