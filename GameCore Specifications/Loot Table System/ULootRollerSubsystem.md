# ULootRollerSubsystem

`UGameInstanceSubsystem`. Single roll authority. Owns modifier registration and audit dispatch. Server-only at runtime — no client calls.

**File:** `LootTable/ULootRollerSubsystem.h`

```cpp
UCLASS()
class GAMECORE_API ULootRollerSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // ── Primary Entry Point ──────────────────────────────────────────────────

    /**
     * Rolls a loot table and returns all produced rewards.
     * Server-only — no-op with empty result on client.
     *
     * @param Table    The loot table asset to roll. Must not be null.
     * @param Context  Roll identity, luck modifier, seed. Caller resolves LuckBonus
     *                 via ResolveLuckBonus() before calling, or sets it directly.
     * @return         Flat array of all rewards from all rolls and nested tables.
     *                 Empty if no entries qualify. Never null.
     */
    TArray<FLootReward> RunLootTable(
        const ULootTable*       Table,
        const FLootRollContext& Context);

    // ── Luck Modifier API ────────────────────────────────────────────────────

    /**
     * Resolves the combined LuckBonus for a player and source tag.
     * Sums all registered subsystem modifiers matching SourceTag, then adds the
     * GAS Luck attribute value from Instigator->GetPawn()'s ASC (if present).
     * The GAS attribute is the authoritative ceiling — no additional cap is applied.
     * Callers pass the result into FLootRollContext::LuckBonus.
     */
    float ResolveLuckBonus(
        APlayerState*    Instigator,
        FGameplayTag     SourceTag) const;

    /**
     * Register a luck modifier for a specific loot source context.
     * Called by buff systems, event managers, or seasonal modifiers.
     *
     * @param ContextTag  The source tag this modifier applies to.
     *                    Use a parent tag (e.g. GameCore.LootSource) to match all children.
     * @param Bonus       Additive bonus to roll ceiling. Must be >= 0.
     * @return            Handle — store and pass to UnregisterModifier to remove.
     */
    FLootModifierHandle RegisterModifier(
        FGameplayTag ContextTag,
        float        Bonus);

    void UnregisterModifier(FLootModifierHandle Handle);

private:

    // Internal recursive roller. Depth is incremented per nested table call.
    // Returns empty array without assert in shipping when depth > MaxRecursionDepth.
    TArray<FLootReward> RollTableInternal(
        const ULootTable*       Table,
        const FLootRollContext& Context,
        int32                   CurrentDepth,
        FRandomStream*          Stream);   // null when no seed

    static constexpr int32 MaxRecursionDepth = 3;

    // Registered luck modifiers. Keyed by handle for O(1) removal.
    TMap<FLootModifierHandle, FLootModifier> Modifiers;
};
```

---

## Roll Algorithm Detail

```
RunLootTable(Table, Context):
  Guard: HasAuthority() — return {} on client
  Guard: Table != nullptr — ensure() + return {}

  Stream = null
  If Context.Seed is set:
    RandomOffset = FMath::RandRange(0, MAX_int32)   // rolled unconditionally before any table entry
    FinalSeed    = HashCombine(Context.Seed.GetValue(), RandomOffset)
    Stream       = FRandomStream(FinalSeed)

  Results = RollTableInternal(Table, Context, 0, Stream)

  // Audit — always fires, even when Results is empty
  FGameCoreBackend::GetAudit(TAG_Audit_Loot_Roll).RecordEvent(
    Instigator:   Context.Instigator
    SourceTag:    Context.SourceTag
    TableAsset:   Table
    FinalSeed:    FinalSeed (or INDEX_NONE if no seed)
    LuckBonus:    Context.LuckBonus
    Results:      Results)

  return Results

RollTableInternal(Table, Context, Depth, Stream):
  if Depth > MaxRecursionDepth:
    ensure(false)   // non-shipping only
    return {}

  RollCount = RandRange(Table.RollCount.Min, Table.RollCount.Max)   // uniform

  For i in [0, RollCount):
    RolledValue = (Stream != null)
        ? Stream->FRandRange(0.0f, 1.0f + Context.LuckBonus)
        : FMath::FRandRange(0.0f, 1.0f + Context.LuckBonus)

    // Evaluate entries in ascending threshold order
    SelectedEntry = null
    For each Entry in Table.Entries (sorted ascending by RollThreshold):
      if Entry.RollThreshold > RolledValue: break
      if not EvaluateRequirements(Entry.EntryRequirements, Context.Instigator): continue
      SelectedEntry = Entry

    if SelectedEntry == null: continue   // no reward this roll

    if SelectedEntry.NestedTable is set:
      NestedTable = SelectedEntry.NestedTable.LoadSynchronous()
      if NestedTable:
        Results += RollTableInternal(NestedTable, Context, Depth + 1, Stream)
    else:
      Quantity = ResolveQuantity(SelectedEntry.Quantity, SelectedEntry.QuantityDistribution)
      Results += FLootReward
      {
        RewardType:       SelectedEntry.Reward.RewardType
        RewardDefinition: SelectedEntry.Reward.RewardDefinition
        Quantity:         Quantity
      }

  return Results
```

---

## Quantity Resolution

```cpp
int32 ResolveQuantity(FInt32Range Range, EQuantityDistribution Distribution)
{
    const int32 Min = Range.GetLowerBoundValue();
    const int32 Max = Range.GetUpperBoundValue();
    if (Min == Max) return Min;

    switch (Distribution)
    {
    case EQuantityDistribution::Uniform:
        return FMath::RandRange(Min, Max);

    case EQuantityDistribution::Normal:
        // Triangular approximation: average of two uniform rolls.
        // No external dependency, bias toward midpoint.
        const int32 A = FMath::RandRange(Min, Max);
        const int32 B = FMath::RandRange(Min, Max);
        return FMath::RoundToInt((A + B) * 0.5f);
    }
    return Min;
}
```

---

## Modifier Types

```cpp
struct FLootModifier
{
    FGameplayTag ContextTag;   // source tag this bonus applies to (hierarchy-matched)
    float        Bonus;        // additive roll ceiling extension, >= 0
};

// Opaque handle returned by RegisterModifier. Store to unregister.
struct FLootModifierHandle
{
    uint32 Id = 0;
    bool IsValid() const { return Id != 0; }
};
```

`ResolveLuckBonus` sums all `FLootModifier` entries where `SourceTag.MatchesTag(Modifier.ContextTag)`, then adds the GAS `Attribute.Luck` value from the instigator's ASC (if present). **No cap is applied by the loot system** — the GAS attribute and buff design are the authoritative ceiling. Negative totals are clamped to 0.0.

---

## Authority

`RunLootTable` is a no-op returning an empty array on the client. `HasAuthority()` is checked at the top of the call. No `BlueprintCallable` exposure — C++ server-side API only.

---

## Gameplay Tags

```
GameCore.LootSource.BossKill
GameCore.LootSource.ChestOpen
GameCore.LootSource.QuestReward
GameCore.LootSource.Fishing
GameCore.LootSource.Crafting

GameCore.Audit.Loot.Roll
```

Game-specific source tags extend `GameCore.LootSource` in the game module's tag file.
