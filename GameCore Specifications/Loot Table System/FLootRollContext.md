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
    // INDEX_NONE (default) means unseeded — FMath::FRand() is used.
    // Any other value activates seeded rolling via a single FRandomStream
    // derived from this value. The FinalSeed is recorded in the audit payload
    // for CS reproduction of historical rolls.
    UPROPERTY(BlueprintReadWrite)
    int32 Seed = INDEX_NONE;
};
```

---

## Seed Flow

The seed in context is a **designer/CS-provided base value**, not the raw stream seed. The roller derives the actual stream seed once at the start of `RunLootTable`, before any table entries are processed:

```
If Context.Seed != INDEX_NONE:
    RandomOffset = FMath::RandRange(0, MAX_int32)   // rolled once, unconditionally
    FinalSeed    = HashCombine(Context.Seed, RandomOffset)
    RollStream   = FRandomStream(FinalSeed)          // single stream, advanced sequentially
    All rolls (across all RollCount iterations and nested tables) use RollStream.FRandRange()
Else:
    All rolls use FMath::FRandRange() — no stream
```

Using a **single advancing stream** rather than per-roll seeding ensures that the full sequence of rolls across a table invocation is reproducible from one `FinalSeed`. The `FinalSeed` is recorded in the audit payload, enabling exact reproduction by CS tooling.

---

## LuckBonus Resolution (Caller Responsibility)

The caller resolves `LuckBonus` before constructing the context:

```
LuckBonus = ULootRollerSubsystem::ResolveLuckBonus(Instigator, SourceTag)
```

`ResolveLuckBonus` sums:
- Registered subsystem modifiers matching `SourceTag`
- GAS Luck attribute on `Instigator->GetPawn()`'s ASC (if present)

Callers may override or bypass this and set `LuckBonus` directly for scripted scenarios.
