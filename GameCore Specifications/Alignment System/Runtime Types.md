# Runtime Types

**Sub-page of:** [Alignment System](../Alignment%20System.md)

This page covers the three runtime structs used by `UAlignmentComponent`: the batch mutation input (`FAlignmentDelta`), the per-axis live data entry (`FAlignmentEntry`), and the replicated container (`FAlignmentArray`).

**File:** `Alignment/AlignmentTypes.h`

---

## `FAlignmentDelta` — Batch Mutation Input

```cpp
// One axis change within a batch call to UAlignmentComponent::ApplyAlignmentDeltas.
// Positive delta moves toward max; negative delta moves toward min.
USTRUCT(BlueprintType)
struct GAMECORE_API FAlignmentDelta
{
    GENERATED_BODY()

    // The axis to mutate. Must match a registered UAlignmentDefinition::AlignmentTag.
    // If no definition is registered for this tag, the delta is silently skipped.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
    FGameplayTag AlignmentTag;

    // The amount to add to the underlying value.
    // Positive = toward max (e.g. toward "good" on a good/evil axis).
    // Negative = toward min (e.g. toward "evil").
    // The result is clamped to [SaturatedMin, SaturatedMax] after application.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
    float Delta = 0.f;
};
```

---

## `FAlignmentEntry` — Per-Axis Live Data

```cpp
// Per-axis runtime state for one player. Lives inside FAlignmentArray.
// One entry per registered UAlignmentDefinition.
USTRUCT(BlueprintType)
struct GAMECORE_API FAlignmentEntry : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Matches UAlignmentDefinition::AlignmentTag. Used as the lookup key.
    UPROPERTY()
    FGameplayTag AlignmentTag;

    // Raw accumulated value. May exceed the effective range.
    // Clamped to [SaturatedMin, SaturatedMax] on every mutation.
    // Persisted directly — no lossy conversion.
    UPROPERTY()
    float UnderlyingValue = 0.f;

    // Derived at query time — never stored or replicated separately.
    // Returns Clamp(UnderlyingValue, Def.EffectiveMin, Def.EffectiveMax).
    float GetEffectiveValue(const UAlignmentDefinition& Def) const
    {
        return FMath::Clamp(UnderlyingValue, Def.EffectiveMin, Def.EffectiveMax);
    }
};
```

### Why Not Replicate the Effective Value?

Effective value is a deterministic function of `UnderlyingValue` and the definition asset, which loads identically on all machines. Storing or replicating effective separately would create a second source of truth and introduce sync risk with zero benefit.

---

## `FAlignmentArray` — Replicated Container

```cpp
// FFastArraySerializer container for per-player alignment runtime data.
// Replicated as a property on UAlignmentComponent (lives on APlayerState).
// Only dirty items are sent over the wire per delta update.
USTRUCT()
struct GAMECORE_API FAlignmentArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FAlignmentEntry> Items;

    // Lookup by tag. Returns nullptr if not found.
    FAlignmentEntry* FindByTag(FGameplayTag Tag)
    {
        return Items.FindByPredicate([&](const FAlignmentEntry& E){ return E.AlignmentTag == Tag; });
    }

    const FAlignmentEntry* FindByTag(FGameplayTag Tag) const
    {
        return Items.FindByPredicate([&](const FAlignmentEntry& E){ return E.AlignmentTag == Tag; });
    }

    // Required by FFastArraySerializer.
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FAlignmentEntry, FAlignmentArray>(
            Items, DeltaParms, *this);
    }
};

// Required trait declaration for FFastArraySerializer.
template<>
struct TStructOpsTypeTraits<FAlignmentArray> : public TStructOpsTypeTraitsBase2<FAlignmentArray>
{
    enum { WithNetDeltaSerializer = true };
};
```

---

## Lifecycle

1. On `RegisterAlignment`, a new `FAlignmentEntry` is appended to `FAlignmentArray::Items` (if one does not already exist for that tag). `UnderlyingValue` starts at `0`.
2. On `ApplyAlignmentDeltas`, matching entries are mutated in-place and `MarkItemDirty` is called for each changed entry.
3. `FFastArraySerializer` replicates only dirty items to the owning client at the next network update.
4. On the client, `OnRep_AlignmentData` fires after any batch of updates arrives.

---

## Serialization Notes

`UnderlyingValue` must be included in any persistence layer that saves player data. When loading from a save:

```cpp
// Restore from save record — call before BeginPlay so initial replication is correct.
void UAlignmentComponent::LoadFromSave(const FAlignmentSaveRecord& Record)
{
    for (const FAlignmentSaveEntry& SaveEntry : Record.Entries)
    {
        if (FAlignmentEntry* Entry = AlignmentData.FindByTag(SaveEntry.AlignmentTag))
        {
            Entry->UnderlyingValue = SaveEntry.UnderlyingValue;
            AlignmentData.MarkItemDirty(*Entry);
        }
    }
}
```

The save record should store `AlignmentTag` + `UnderlyingValue` pairs only. Effective value is never persisted.
