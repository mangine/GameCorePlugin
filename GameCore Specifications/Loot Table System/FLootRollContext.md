# FLootRollContext

Passed by the caller into `ULootRollerSubsystem::RunLootTable`. Carries roll identity, modifier input, and optional determinism seed.

**File:** `LootTable/FLootRollContext.h`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FLootRollContext
{
    GENERATED_BODY()

    // The player receiving the rewards. Used for requirement evaluation and audit.
    // Must be valid on the server at roll time.
    UPROPERTY(BlueprintReadWrite)
    TWeakObjectPtr<APlayerState> Instigator;

    // Why this roll is happening. Used for modifier lookup and audit.
    // Examples: GameCore.LootSource.BossKill, GameCore.LootSource.ChestOpen,
    //           GameCore.LootSource.QuestReward, GameCore.LootSource.Fishing
    // Optional — omitting disables context-specific modifiers.
    UPROPERTY(BlueprintReadWrite)
    FGameplayTag SourceTag;

    // Extends the roll ceiling from 1.0 to (1.0 + LuckBonus).
    // Resolved by the caller from player buffs, event multipliers, and registered
    // subsystem modifiers before calling RunLootTable.
    // Must be >= 0.0. Negative values are clamped to 0.0.
    UPROPERTY(BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float LuckBonus = 0.0f;

    // Optional group actor for group loot scenarios.
    // Null for solo rolls. The fulfillment layer uses this to route to group
    // distribution logic (round-robin, need/greed, etc.) — not used by the roller.
    UPROPERTY(BlueprintReadWrite)
    TWeakObjectPtr<AActor> GroupActor;

    // Optional deterministic seed.
    // When set, the roller generates a final seed by hashing this value with
    // a random offset (rolled first, before any table entries), then uses
    // FRandomStream(FinalSeed + RollIndex) per entry roll.
    // Used for CS reproduction of historical loot rolls.
    // When unset, FMath::FRand() is used.
    UPROPERTY(BlueprintReadWrite)
    TOptional<int32> Seed;
};
```

---

## Seed Flow

The seed in context is a **designer/CS-provided base value**, not the raw stream seed. The roller derives the actual stream seed:

```
If Context.Seed is set:
    RandomOffset = FMath::RandRange(0, MAX_int32)   // rolled first, unconditionally
    FinalSeed    = HashCombine(Context.Seed.GetValue(), RandomOffset)
    RollStream   = FRandomStream(FinalSeed)
    Per-entry roll i: RollStream.FRandRange(0.0, 1.0 + LuckBonus)
Else:
    Per-entry roll: FMath::FRandRange(0.0, 1.0 + LuckBonus)
```

The `FinalSeed` is recorded in the audit payload, enabling exact reproduction by CS tooling.

---

## LuckBonus Resolution (Caller Responsibility)

The caller resolves `LuckBonus` before constructing the context:

```
LuckBonus = ULootRollerSubsystem::ResolveLuckBonus(Instigator, SourceTag)
```

`ResolveLuckBonus` sums:
- Registered subsystem modifiers matching `SourceTag`
- GAS attribute modifier on `Instigator->GetPawn()` (if ASC present)

Callers may override or bypass this and set `LuckBonus` directly for scripted scenarios.
