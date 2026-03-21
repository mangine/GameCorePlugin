# Loot Table System — Usage

---

## 1. Rolling a Loot Table

All rolling is server-only. Never call `RunLootTable` from client code.

```cpp
// In a server-side game class (e.g. after killing a boss, opening a chest)
void UMyLootDispatcher::GrantLootToPlayer(
    APlayerState*   Recipient,
    ULootTable*     Table,
    FGameplayTag    SourceTag)  // e.g. TAG_LootSource_BossKill
{
    ULootRollerSubsystem* Roller =
        GetGameInstance()->GetSubsystem<ULootRollerSubsystem>();

    // 1. Resolve luck bonus — sums registered modifiers + GAS Attribute.Luck
    FLootRollContext Context;
    Context.Instigator = Recipient;
    Context.SourceTag  = SourceTag;
    Context.LuckBonus  = Roller->ResolveLuckBonus(Recipient, SourceTag);
    // Context.Seed = INDEX_NONE (unseeded, default)

    // 2. Roll
    TArray<FLootReward> Rewards = Roller->RunLootTable(Table, Context);

    // 3. Fulfill — the game layer routes each reward
    FulfillRewards(Recipient, Rewards, Context);
}
```

---

## 2. Fulfilling Rewards

The loot system returns a flat `TArray<FLootReward>`. The game layer switches on `RewardType` and dispatches to the appropriate system.

```cpp
void UMyLootDispatcher::FulfillRewards(
    APlayerState*              Recipient,
    const TArray<FLootReward>& Rewards,
    const FLootRollContext&    Context)
{
    for (const FLootReward& Reward : Rewards)
    {
        if (!Reward.IsValid()) continue;

        if (Reward.RewardType.MatchesTag(TAG_Reward_Item))
        {
            // Load async in production; sync shown for brevity
            UItemDefinition* Def =
                Cast<UItemDefinition>(Reward.RewardDefinition.LoadSynchronous());
            if (Def)
                InventorySystem->AddItem(Recipient, Def, Reward.Quantity);
        }
        else if (Reward.RewardType.MatchesTag(TAG_Reward_XP))
        {
            UXPRewardDefinition* Def =
                Cast<UXPRewardDefinition>(Reward.RewardDefinition.LoadSynchronous());
            if (Def)
                ProgressionSubsystem->GrantXP(
                    Recipient, Def->ProgressionTag, Reward.Quantity, ...);
        }
        else if (Reward.RewardType.MatchesTag(TAG_Reward_Currency))
        {
            UCurrencyDefinition* Def =
                Cast<UCurrencyDefinition>(Reward.RewardDefinition.LoadSynchronous());
            if (Def)
                CurrencySystem->Credit(Recipient, Def, Reward.Quantity);
        }
        // Add more reward types here — no GameCore changes required
    }
}
```

---

## 3. Group Loot

Pass the group actor in context. The fulfillment layer uses it for distribution logic (round-robin, need/greed, master looter) — the roller produces a flat reward list regardless.

```cpp
Context.GroupActor = GetGroupComponent()->GetGroupActor();
TArray<FLootReward> Rewards = Roller->RunLootTable(Table, Context);
// Fulfillment layer reads Context.GroupActor to route distribution
```

---

## 4. Seeded (Reproducible) Rolling

Used for CS investigation or scripted event rewards.

```cpp
Context.Seed = StoredRollSeed;  // any int32 != INDEX_NONE
TArray<FLootReward> Rewards = Roller->RunLootTable(Table, Context);
// To reproduce: same Table asset + same Context.Seed + same FinalSeed from audit
```

The audit channel records `FinalSeed` (derived from `Context.Seed`) and the table asset path. Reproducing the roll requires the same asset state.

---

## 5. Registering Luck Modifiers

Buff systems, seasonal events, and zone managers register modifiers on the subsystem. Modifiers are summed by `ResolveLuckBonus` alongside the GAS `Attribute.Luck`.

```cpp
// Register — e.g. in a buff component's BeginPlay
FLootModifierHandle Handle = Roller->RegisterModifier(
    TAG_LootSource_BossKill,  // applies only to boss kill rolls
    0.25f);                   // extends roll ceiling by 0.25

// Unregister — in EndPlay or when buff expires
Roller->UnregisterModifier(Handle);
```

Use a parent tag (e.g. `GameCore.LootSource`) to match all loot source children.

---

## 6. Making a Reward Asset Loot-Compatible

Implement `ILootRewardable` on any `UObject`-derived asset class — one line, no hierarchy restructuring.

```cpp
// ItemDefinition.h
UCLASS()
class UItemDefinition : public UPrimaryDataAsset, public ILootRewardable
{
    GENERATED_BODY()
    // ... existing fields unchanged
};
```

The asset now appears in the `RewardDefinition` picker in loot table entries.

---

## 7. Creating a Loot Table Asset

1. In the Content Browser, create a new `ULootTable` data asset.
2. Set `RollCount` (e.g. `FInt32Range(1, 3)` = roll 1-3 times).
3. Add entries, each with:
   - `RollThreshold` — position in [0.0, ∞). Values > 1.0 are luck-gated.
   - `Reward.RewardType` — tag for fulfillment routing.
   - `Reward.RewardDefinition` — the concrete asset (must implement `ILootRewardable`).
   - `Quantity` range and `QuantityDistribution`.
   - Optionally `EntryRequirements` and `bDowngradeOnRequirementFailed`.
4. Click **Sort Entries** in the Details panel to sort ascending by threshold.
5. Save — `IsDataValid` auto-sorts and errors on duplicate thresholds.

### Threshold layout example
```
0.00 — Common drop    (always reachable; fills dead zone at bottom)
0.30 — Uncommon drop  (reachable on roll >= 0.30)
0.65 — Rare drop      (reachable on roll >= 0.65)
0.90 — Epic drop      (reachable on roll >= 0.90)
1.10 — Luck-gated drop (unreachable until LuckBonus > 0.10)
```

> Gaps (e.g. 0.30 → 0.65) are dead zones: a roll of 0.45 produces no reward unless threshold 0.30 wins.
> An entry at threshold 0.00 guarantees at least one candidate is always found.

---

## 8. Adding a Nested Table Entry

Set `NestedTable` on an entry instead of `Reward`. When that entry is selected the nested table is rolled recursively (max depth 3).

```
Entry RollThreshold=0.80:
  NestedTable → DA_RareChestContents
  // Reward is ignored when NestedTable is set
```

Ensure nested table assets are loaded before rolling (i.e. they are referenced by an actor already in memory).

---

## 9. Gameplay Tags

```
// Loot source — used in FLootRollContext::SourceTag and modifier registration
GameCore.LootSource.BossKill
GameCore.LootSource.ChestOpen
GameCore.LootSource.QuestReward
GameCore.LootSource.Fishing
GameCore.LootSource.Crafting

// Audit — passed to FGameCoreBackend::GetAudit()
GameCore.Audit.Loot.Roll

// Reward routing — used in FLootEntryReward::RewardType and FLootReward::RewardType
GameCore.Reward.Item
GameCore.Reward.Currency
GameCore.Reward.XP
GameCore.Reward.Ability
```

Game-specific tags extend these hierarchies in the game module's tag file.
