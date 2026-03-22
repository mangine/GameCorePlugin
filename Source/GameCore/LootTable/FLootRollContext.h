// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/PlayerState.h"
#include "FLootRollContext.generated.h"

/**
 * FLootRollContext
 *
 * Passed by the caller into ULootRollerSubsystem::RunLootTable.
 * Carries roll identity, luck modifier input, optional group context,
 * and optional determinism seed. Transient — never persisted.
 *
 * Build this struct server-side before calling RunLootTable:
 *   Context.Instigator = PlayerState;
 *   Context.SourceTag  = TAG_LootSource_BossKill;
 *   Context.LuckBonus  = Roller->ResolveLuckBonus(PlayerState, SourceTag);
 */
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
