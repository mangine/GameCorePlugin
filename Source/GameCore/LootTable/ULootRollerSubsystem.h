// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "LootTable/FLootRollContext.h"
#include "LootTable/FLootReward.h"
#include "LootTable/FLootTableEntry.h"
#include "ULootRollerSubsystem.generated.h"

class ULootTable;

/**
 * ULootRollerSubsystem
 *
 * UGameInstanceSubsystem. Single roll authority. Owns luck modifier registration
 * and audit dispatch. Server-only at runtime — never called from client code.
 *
 * Usage:
 *   ULootRollerSubsystem* Roller = GetGameInstance()->GetSubsystem<ULootRollerSubsystem>();
 *   Context.LuckBonus = Roller->ResolveLuckBonus(PlayerState, SourceTag);
 *   TArray<FLootReward> Rewards = Roller->RunLootTable(Table, Context);
 */
UCLASS()
class GAMECORE_API ULootRollerSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ── Primary Entry Point ──────────────────────────────────────────────────

    /**
     * Rolls a loot table and returns all produced rewards.
     * Server-only — returns empty array on the client (HasAuthority() guard).
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
     * then adds the GAS Luck attribute value from Instigator->GetPawn()'s ASC
     * (if present and the GameplayAbilities module is available).
     * No cap applied — GAS attribute and buff design are the authoritative ceiling.
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
     * @param Bonus       Additive bonus to roll ceiling. Negative values are clamped to 0.
     * @return            Handle — store and pass to UnregisterModifier to remove.
     */
    FLootModifierHandle RegisterModifier(FGameplayTag ContextTag, float Bonus);

    /** Removes the modifier associated with Handle. Safe to call with an invalid handle. */
    void UnregisterModifier(FLootModifierHandle Handle);

private:
    /**
     * Recursive roll implementation. Called by RunLootTable.
     * @param Stream  Null when unseeded — FMath random functions are used instead.
     */
    TArray<FLootReward> RollTableInternal(
        const ULootTable*       Table,
        const FLootRollContext& Context,
        int32                   CurrentDepth,
        FRandomStream*          Stream);

    /**
     * Resolves a quantity within Range using the given distribution.
     * Stream-aware to preserve determinism when seeded.
     * @param Stream  Null when unseeded.
     */
    int32 ResolveQuantity(
        FInt32Range            Range,
        EQuantityDistribution  Distribution,
        FRandomStream*         Stream);

    // Maximum nested table recursion depth. Exceeding this triggers ensure(false).
    static constexpr int32 MaxRecursionDepth = 3;

    // Active modifiers. Key is an opaque incrementing uint32 handle ID.
    TMap<FLootModifierHandle, FLootModifier> Modifiers;

    // Monotonic counter for handle generation. Starts at 1 so 0 is always invalid.
    uint32 NextHandleId = 1;
};
