# FLootRollContext

Passed by the caller into `ULootRollerSubsystem::RunLootTable`. Carries roll identity, luck modifier input, optional group context, and optional determinism seed. Transient — never persisted.

**File:** `LootTable/FLootRollContext.h`

---

## Declaration

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FLootRollContext
{
    GENERATED_BODY()

    // The player receiving the rewards. Used for requirement evaluation and audit.
    // Must be valid on the server at roll time.
    // Not a UPROPERTY — TWeakObjectPtr is not safe as UPROPERTY in a transient struct.
    // Assigned in C++ before passing to RunLootTable.
    TWeakObjectPtr<APlayerState> Instigator;

    // Why this roll is happening. Used for modifier lookup and audit.
    // Examples: GameCore.LootSource.BossKill, GameCore.LootSource.ChestOpen,
    //           GameCore.LootSource.QuestReward, GameCore.LootSource.Fishing
    // Optional — omitting disables context-specific modifiers.
    UPROPERTY(BlueprintReadWrite)
    FGameplayTag SourceTag;

    // Extends the roll ceiling from 1.0 to (1.0 + LuckBonus).
    // Resolved by the caller via ResolveLuckBonus() before calling RunLootTable,
    // or set directly for scripted scenarios.
    // Must be >= 0.0. Negative values are clamped to 0.0 by the roller.
    UPROPERTY(BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float LuckBonus = 0.0f;

    // Optional group actor for group loot scenarios.
    // Null for solo rolls. The fulfillment layer uses this to route to group
    // distribution logic (round-robin, need/greed, etc.) — not used by the roller.
    // Not a UPROPERTY — TWeakObjectPtr is not safe as UPROPERTY in a transient struct.
    TWeakObjectPtr<AActor> GroupActor;

    // Optional deterministic seed.
    // INDEX_NONE (default, -1) = unseeded — FMath::FRand() is used.
    // Any other value activates seeded rolling.
    // Note: seed value -1 cannot be explicitly requested; INDEX_NONE is always unseeded.
    // The derived FinalSeed is recorded in the audit payload for CS reproduction.
    UPROPERTY(BlueprintReadWrite)
    int32 Seed = INDEX_NONE;
};
```

---

## Seed Flow

The `Seed` field is a **caller-provided base value**, not the raw stream seed. The roller derives the actual stream seed once at the start of `RunLootTable`, before any table entries are processed:

```
If Context.Seed != INDEX_NONE:
    RandomOffset = FMath::RandRange(0, MAX_int32)   // rolled once, unconditionally
    FinalSeed    = HashCombine(Context.Seed, RandomOffset)
    RollStream   = FRandomStream(FinalSeed)          // single stream, advanced sequentially
    All rolls and quantity resolutions advance RollStream — including nested table recursion
Else:
    All rolls use FMath::FRandRange() — no stream, non-deterministic
```

`FinalSeed` is emitted to the audit channel. To reproduce a roll: same `ULootTable` asset + same `FinalSeed`.

> **Why `RandomOffset`?** The raw caller seed is not directly predictable from the rolled result. An attacker who can observe the roll result cannot trivially reverse-engineer the seed.

---

## LuckBonus Resolution

The caller is responsible for resolving `LuckBonus` before constructing the context:

```cpp
Context.LuckBonus = Roller->ResolveLuckBonus(Instigator, SourceTag);
```

`ResolveLuckBonus` sums:
- All registered `FLootModifier` entries where `SourceTag.MatchesTag(Modifier.ContextTag)`
- The GAS `Attribute.Luck` value from `Instigator->GetPawn()`'s ASC (if present)

Callers may set `LuckBonus` directly to bypass this for scripted scenarios (e.g. story rewards where luck should be ignored).
