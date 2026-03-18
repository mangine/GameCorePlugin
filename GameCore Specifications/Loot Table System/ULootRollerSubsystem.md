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
        APlayerState* Instigator,
        FGameplayTag  SourceTag) const;

    /**
     * Register a luck modifier for a specific loot source context.
     * Called by buff systems, event managers, or seasonal modifiers.
     *
     * @param ContextTag  Source tag this modifier applies to.
     *                    Use a parent tag (e.g. GameCore.LootSource) to match all children.
     * @param Bonus       Additive bonus to roll ceiling. Must be >= 0.
     * @return            Handle — store and pass to UnregisterModifier to remove.
     */
    FLootModifierHandle RegisterModifier(FGameplayTag ContextTag, float Bonus);
    void UnregisterModifier(FLootModifierHandle Handle);

private:

    TArray<FLootReward> RollTableInternal(
        const ULootTable*       Table,
        const FLootRollContext& Context,
        int32                   CurrentDepth,
        FRandomStream*          Stream);   // null when unseeded

    // Quantity resolution is stream-aware to preserve determinism when seeded.
    int32 ResolveQuantity(
        FInt32Range          Range,
        EQuantityDistribution Distribution,
        FRandomStream*       Stream);      // null when unseeded

    static constexpr int32 MaxRecursionDepth = 3;

    TMap<FLootModifierHandle, FLootModifier> Modifiers;
};
```

---

## Roll Algorithm Detail

```
RunLootTable(Table, Context):
  Guard: HasAuthority() — return {} on client
  Guard: Table != nullptr — ensure() + return {}

  // Seed setup — done once before any rolls
  Stream = null
  FinalSeed = INDEX_NONE
  If Context.Seed != INDEX_NONE:
    RandomOffset = FMath::RandRange(0, MAX_int32)
    FinalSeed    = HashCombine(Context.Seed, RandomOffset)
    Stream       = FRandomStream(FinalSeed)   // single stream, advanced throughout

  Results = RollTableInternal(Table, Context, 0, Stream)

  // Audit — always fires, even when Results is empty
  FGameCoreBackend::GetAudit(TAG_GameCore_Audit_Loot_Roll).RecordEvent(
    Instigator:   Context.Instigator
    SourceTag:    Context.SourceTag
    TableAsset:   Table
    FinalSeed:    FinalSeed
    LuckBonus:    Context.LuckBonus
    Results:      Results)

  return Results


RollTableInternal(Table, Context, Depth, Stream):
  if Depth > MaxRecursionDepth:
    ensure(false)   // non-shipping only
    return {}

  RollCount = Stream ? Stream->RandRange(Table.RollCount.Min, Table.RollCount.Max)
                     : FMath::RandRange(Table.RollCount.Min, Table.RollCount.Max)

  For each roll:
    RolledValue = Stream ? Stream->FRandRange(0.f, 1.f + Context.LuckBonus)
                         : FMath::FRandRange(0.f, 1.f + Context.LuckBonus)

    // ── Entry selection with downgrade support ────────────────────────────
    // Step 1: find the highest entry whose threshold does not exceed RolledValue.
    // Step 2: evaluate EntryRequirements on that candidate.
    // Step 3: on failure — downgrade walk if bDowngradeOnRequirementFailed, else no reward.

    FRequirementContext ReqContext = FRequirementContext::ForActor(
        Context.Instigator.IsValid() ? Context.Instigator->GetPawn() : nullptr);

    // Find initial candidate (highest threshold <= RolledValue)
    CandidateIndex = INDEX_NONE
    For i = Table.Entries.Num()-1 downto 0:
      if Table.Entries[i].RollThreshold <= RolledValue:
        CandidateIndex = i
        break

    // Downgrade loop
    while CandidateIndex >= 0:
      Candidate = Table.Entries[CandidateIndex]
      if URequirementLibrary::EvaluateAll(Candidate.EntryRequirements, ReqContext):
        break   // requirements passed — use this entry
      else if Candidate.bDowngradeOnRequirementFailed:
        CandidateIndex -= 1   // walk down
      else:
        CandidateIndex = INDEX_NONE
        break

    if CandidateIndex < 0: CandidateIndex = INDEX_NONE
    if CandidateIndex == INDEX_NONE: continue   // no reward this roll

    SelectedEntry = Table.Entries[CandidateIndex]

    if SelectedEntry.NestedTable is set:
      NestedTable = SelectedEntry.NestedTable.LoadSynchronous()
      if NestedTable:
        // Recurse — stream is passed through so nested rolls advance the same sequence.
        // If the nested table produces no rewards, no further downgrade occurs —
        // downgrade applies only to direct entry selection, not nested table output.
        Results += RollTableInternal(NestedTable, Context, Depth + 1, Stream)
    else:
      Quantity = ResolveQuantity(SelectedEntry.Quantity, SelectedEntry.QuantityDistribution, Stream)
      Results += FLootReward
      {
        RewardType:       SelectedEntry.Reward.RewardType
        RewardDefinition: SelectedEntry.Reward.RewardDefinition
        Quantity:         Quantity
      }

  return Results
```

---

## Requirement Context Construction

`FRequirementContext::ForActor` is the standard builder used across the plugin. In the loot roller it is constructed from the instigator's pawn:

```cpp
FRequirementContext ReqContext = FRequirementContext::ForActor(
    Context.Instigator.IsValid() ? Context.Instigator->GetPawn() : nullptr);
```

If `Instigator` is null or stale the context is built with a null actor — requirements that need a valid actor will fail, which is the correct safe fallback. Requirements that are actor-independent still evaluate normally.

---

## Quantity Resolution

`ResolveQuantity` receives the active `FRandomStream*` so that quantity rolls are part of the same deterministic sequence when seeded. When unseeded, `Stream` is null and `FMath::RandRange` is used.

```cpp
int32 ULootRollerSubsystem::ResolveQuantity(
    FInt32Range           Range,
    EQuantityDistribution Distribution,
    FRandomStream*        Stream)
{
    const int32 Min = Range.GetLowerBoundValue();
    const int32 Max = Range.GetUpperBoundValue();
    if (Min == Max) return Min;

    auto Roll = [&]() -> int32
    {
        return Stream ? Stream->RandRange(Min, Max) : FMath::RandRange(Min, Max);
    };

    switch (Distribution)
    {
    case EQuantityDistribution::Uniform:
        return Roll();

    case EQuantityDistribution::Normal:
        // Triangular approximation: average of two uniform rolls.
        // Bias toward midpoint, no external dependency.
        return FMath::RoundToInt((Roll() + Roll()) * 0.5f);
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

// Opaque handle returned by RegisterModifier. Store and pass to UnregisterModifier.
// GetTypeHash is defined so FLootModifierHandle can be used as a TMap key.
struct FLootModifierHandle
{
    uint32 Id = 0;
    bool IsValid() const { return Id != 0; }
};

INLINE uint32 GetTypeHash(const FLootModifierHandle& Handle)
{
    return GetTypeHash(Handle.Id);
}
```

`ResolveLuckBonus` sums all `FLootModifier` entries where `SourceTag.MatchesTag(Modifier.ContextTag)`, then adds the GAS `Attribute.Luck` value from the instigator's ASC (if present). **No cap is applied by the loot system** — the GAS attribute and buff design are the authoritative ceiling. Negative totals are clamped to 0.0.

---

## Authority

`RunLootTable` is a no-op returning an empty array on the client. `HasAuthority()` is checked at the top of the call. No `BlueprintCallable` exposure — C++ server-side API only.

---

## Gameplay Tags

```
// Loot source tags — used in FLootRollContext::SourceTag and modifier registration
GameCore.LootSource.BossKill
GameCore.LootSource.ChestOpen
GameCore.LootSource.QuestReward
GameCore.LootSource.Fishing
GameCore.LootSource.Crafting

// Audit tag — passed to FGameCoreBackend::GetAudit()
GameCore.Audit.Loot.Roll

// Reward routing tags — used in FLootEntryReward::RewardType and FLootReward::RewardType
GameCore.Reward.Item
GameCore.Reward.Currency
GameCore.Reward.XP
GameCore.Reward.Ability
```

Game-specific tags extend these hierarchies in the game module's tag file.

---

## Asset Manager Registration

Add to `DefaultGame.ini` to enable `ULootTable` async loading and Asset Manager discovery:

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(PrimaryAssetType="LootTable",AssetBaseClass=/Script/GameCore.LootTable,bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/LootTables")),Rules=(Priority=0,bApplyRecursively=True))
```

Adjust `Directories` to match the project's content layout. The type name `"LootTable"` must match `ULootTable`'s `GetPrimaryAssetId()` implementation.
