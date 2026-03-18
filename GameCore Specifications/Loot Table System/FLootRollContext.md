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
    // Not reflected — TWeakObjectPtr is not safe as a UPROPERTY in a transient struct.
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
    // Must be >= 0.0. Negative values are clamped to 0.0.
    UPROPERTY(BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float LuckBonus = 0.0f;

    // Optional group actor for group loot scenarios.
    // Null for solo rolls. The fulfillment layer uses this to route to group
    // distribution logic (round-robin, need/greed, etc.) — not used by the roller.
    // Not reflected — TWeakObjectPtr is not safe as a UPROPERTY in a transient struct.
    TWeakObjectPtr<AActor> GroupActor;

    // Optional deterministic seed.
    // INDEX_NONE (default, -1) means unseeded — FMath::FRand() is used.
    // Any other value activates seeded rolling. Note: there is no way to explicitly
    // request seed value -1; INDEX_NONE is always treated as unseeded.
    // The derived FinalSeed is recorded in the audit payload for CS reproduction.
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
    All rolls and quantity resolutions use RollStream — including nested table recursion
Else:
    All rolls use FMath::FRandRange() — no stream
```

Using a **single advancing stream** ensures that the full sequence of rolls across a table invocation — including nested tables and quantity resolution — is reproducible from one `FinalSeed`. The `FinalSeed` is recorded in the audit payload.

---

## LuckBonus Resolution (Caller Responsibility)

The caller resolves `LuckBonus` before constructing the context:

```
LuckBonus = ULootRollerSubsystem::ResolveLuckBonus(Instigator, SourceTag)
```

`ResolveLuckBonus` sums:
- Registered subsystem modifiers matching `SourceTag`
- GAS Luck attribute on `Instigator->GetPawn()`'s ASC (if present)

Callers may set `LuckBonus` directly to bypass this for scripted scenarios.
