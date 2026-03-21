# ULootRollerSubsystem

`UGameInstanceSubsystem`. Single roll authority. Owns luck modifier registration and audit dispatch. Server-only at runtime — never called from client code.

**File:** `LootTable/ULootRollerSubsystem.h`

---

## Declaration

```cpp
UCLASS()
class GAMECORE_API ULootRollerSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // ── Primary Entry Point ──────────────────────────────────────────────────

    /**
     * Rolls a loot table and returns all produced rewards.
     * Server-only — no-op returning empty array on the client.
     *
     * @param Table    The loot table asset to roll. Must not be null.
     * @param Context  Roll identity, luck modifier, seed. Caller resolves LuckBonus
     *                 via ResolveLuckBonus() before calling, or sets it directly.
     * @return         Flat array of all rewards from all rolls and nested tables.
     *                 Empty if no entries qualify or called on client. Never null.
     */
    TArray<FLootReward> RunLootTable(
        const ULootTable*       Table,
        const FLootRollContext& Context);

    // ── Luck Modifier API ────────────────────────────────────────────────────

    /**
     * Resolves the combined LuckBonus for a player and source tag.
     * Sums all registered subsystem modifiers matching SourceTag (hierarchy match),
     * then adds the GAS Luck attribute value from Instigator->GetPawn()'s ASC.
     * No cap applied — the GAS attribute and buff design are the authoritative ceiling.
     * Negative totals are clamped to 0.0.
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
        FInt32Range            Range,
        EQuantityDistribution  Distribution,
        FRandomStream*         Stream);    // null when unseeded

    static constexpr int32 MaxRecursionDepth = 3;

    // Active modifiers. Key is an opaque incrementing uint32 handle ID.
    TMap<FLootModifierHandle, FLootModifier> Modifiers;

    // Monotonic counter for handle generation.
    uint32 NextHandleId = 1;
};
```

---

## Supporting Types

```cpp
// Internal modifier record. Stored in ULootRollerSubsystem::Modifiers.
struct FLootModifier
{
    FGameplayTag ContextTag;  // source tag this bonus applies to (hierarchy-matched)
    float        Bonus;       // additive roll ceiling extension, >= 0
};

// Opaque handle returned by RegisterModifier. Store and pass to UnregisterModifier.
struct FLootModifierHandle
{
    uint32 Id = 0;
    bool IsValid() const { return Id != 0; }

    bool operator==(const FLootModifierHandle& Other) const { return Id == Other.Id; }
};

INLINE uint32 GetTypeHash(const FLootModifierHandle& Handle)
{
    return GetTypeHash(Handle.Id);
}
```

`RegisterModifier` implementation:
```cpp
FLootModifierHandle ULootRollerSubsystem::RegisterModifier(
    FGameplayTag ContextTag, float Bonus)
{
    FLootModifierHandle Handle;
    Handle.Id = NextHandleId++;
    Modifiers.Add(Handle, FLootModifier{ ContextTag, FMath::Max(0.f, Bonus) });
    return Handle;
}
```

---

## Roll Algorithm

```
RunLootTable(Table, Context):
  Guard: !HasAuthority() → return {}
  Guard: Table == nullptr → ensure(false), return {}

  // Seed setup — done once before any rolls
  Stream    = null
  FinalSeed = INDEX_NONE
  If Context.Seed != INDEX_NONE:
    RandomOffset = FMath::RandRange(0, MAX_int32)
    FinalSeed    = HashCombine(Context.Seed, RandomOffset)
    Stream       = FRandomStream(FinalSeed)

  Results = RollTableInternal(Table, Context, 0, Stream)

  // Audit — always fires, even when Results is empty
  FGameCoreBackend::GetAudit(TAG_GameCore_Audit_Loot_Roll).RecordEvent(
    Instigator: Context.Instigator
    SourceTag:  Context.SourceTag
    TableAsset: Table
    FinalSeed:  FinalSeed
    LuckBonus:  Context.LuckBonus
    Results:    Results)

  return Results


RollTableInternal(Table, Context, Depth, Stream):
  if Depth > MaxRecursionDepth:
    ensure(false)   // non-shipping only
    return {}

  RollCount = (Stream ? Stream->RandRange : FMath::RandRange)(
                  Table.RollCount.GetLowerBoundValue(),
                  Table.RollCount.GetUpperBoundValue())

  for each roll in [0, RollCount):
    RolledValue = (Stream ? Stream->FRandRange : FMath::FRandRange)(
                      0.f, 1.f + FMath::Max(0.f, Context.LuckBonus))

    // Find highest threshold entry that does not exceed RolledValue
    CandidateIndex = INDEX_NONE
    for i = Table.Entries.Num()-1 downto 0:
      if Table.Entries[i].RollThreshold <= RolledValue:
        CandidateIndex = i
        break

    if CandidateIndex == INDEX_NONE:
      continue   // dead zone or all thresholds exceed rolled value

    // Requirement evaluation + downgrade walk
    ReqContext = FRequirementContext::ForActor(
        Context.Instigator.IsValid() ? Context.Instigator->GetPawn() : nullptr)

    while CandidateIndex >= 0:
      Candidate = Table.Entries[CandidateIndex]
      if URequirementLibrary::EvaluateAll(Candidate.EntryRequirements, ReqContext):
        break   // requirements passed
      else if Candidate.bDowngradeOnRequirementFailed:
        CandidateIndex -= 1
      else:
        CandidateIndex = INDEX_NONE
        break

    if CandidateIndex < 0: CandidateIndex = INDEX_NONE
    if CandidateIndex == INDEX_NONE: continue

    SelectedEntry = Table.Entries[CandidateIndex]

    ensure(!SelectedEntry.Reward.RewardType.IsValid() || SelectedEntry.NestedTable.IsNull())

    if SelectedEntry.NestedTable is set:
      NestedTable = SelectedEntry.NestedTable.LoadSynchronous()
      if NestedTable:
        Results += RollTableInternal(NestedTable, Context, Depth+1, Stream)
    else:
      Quantity = ResolveQuantity(
                     SelectedEntry.Quantity,
                     SelectedEntry.QuantityDistribution,
                     Stream)
      Results += FLootReward
      {
        RewardType:       SelectedEntry.Reward.RewardType
        RewardDefinition: SelectedEntry.Reward.RewardDefinition
        Quantity:         Quantity
      }

  return Results
```

---

## `ResolveQuantity` Implementation

```cpp
int32 ULootRollerSubsystem::ResolveQuantity(
    FInt32Range            Range,
    EQuantityDistribution  Distribution,
    FRandomStream*         Stream)
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
        // Biases toward midpoint, no external library dependency.
        return FMath::RoundToInt((Roll() + Roll()) * 0.5f);
    }
    return Min;
}
```

---

## Requirement Context Construction

```cpp
FRequirementContext ReqContext = FRequirementContext::ForActor(
    Context.Instigator.IsValid() ? Context.Instigator->GetPawn() : nullptr);
```

If `Instigator` is null or stale, the context is built with a null actor. Requirements that require a valid actor will fail — this is the correct safe fallback. Requirements that are actor-independent still evaluate normally.

---

## Authority

`RunLootTable` is server-only. `HasAuthority()` is checked at entry and returns `{}` on the client. No `BlueprintCallable` exposure — C++ server-side API only.

---

## Gameplay Tags

```
// Loot source tags
GameCore.LootSource.BossKill
GameCore.LootSource.ChestOpen
GameCore.LootSource.QuestReward
GameCore.LootSource.Fishing
GameCore.LootSource.Crafting

// Audit tag
GameCore.Audit.Loot.Roll
```
