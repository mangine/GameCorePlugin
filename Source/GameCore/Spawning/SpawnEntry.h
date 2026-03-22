#pragma once

#include "CoreMinimal.h"
#include "Requirements/RequirementList.h"
#include "SpawnEntry.generated.h"

class ULootTable;

/**
 * Per-entity-type configuration struct.
 * One entry describes a single entity class USpawnManagerComponent should keep alive,
 * along with count limits, per-tick spawn cap, eligibility requirements, and loot override.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FSpawnEntry
{
    GENERATED_BODY()

    // ── Entity Class ──────────────────────────────────────────────────────────

    /**
     * The actor class to spawn. Must implement ISpawnableEntity.
     * Soft reference — loaded asynchronously at BeginPlay.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry")
    TSoftClassPtr<AActor> EntityClass;

    // ── Count Limits ─────────────────────────────────────────────────────────

    /** Maximum number of live instances of this entry at any time. */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry", meta = (ClampMin = 1))
    int32 MaxCount = 1;

    /**
     * Maximum spawns of this entry per flow tick.
     * 0 = no per-entry cap; only the global FlowCount budget applies.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry", meta = (ClampMin = 0))
    int32 MaxPerTick = 0;

    // ── Requirements ─────────────────────────────────────────────────────────

    /**
     * Optional eligibility gate. Evaluated once per flow tick against a
     * world-state-only FRequirementContext (no player data).
     * Null = always eligible.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry")
    TObjectPtr<URequirementList> SpawnRequirements;

    // ── Loot Table Override ──────────────────────────────────────────────────

    /**
     * Optional loot table override. Passed to the entity via ISpawnableEntity::OnSpawnedByManager.
     * The spawn manager does NOT load or use this itself.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry")
    TSoftObjectPtr<ULootTable> LootTableOverride;

    // ── Runtime State (not UPROPERTY — ephemeral, never persisted) ────────────

    /**
     * Weak pointers to currently live instances.
     * NOT a UPROPERTY — serialising stale actor pointers causes issues on level reload.
     */
    TArray<TWeakObjectPtr<AActor>> LiveInstances;

    // ── Runtime Helpers ───────────────────────────────────────────────────────

    /** Prunes invalid weak pointers and returns the current live count. */
    int32 GetAndPruneLiveCount()
    {
        LiveInstances.RemoveAll(
            [](const TWeakObjectPtr<AActor>& P){ return !P.IsValid(); });
        return LiveInstances.Num();
    }

    /** Returns how many more instances can be spawned right now. */
    int32 GetVacancy() const
    {
        return FMath::Max(0, MaxCount - LiveInstances.Num());
    }

    /**
     * Returns the effective per-tick spawn budget for this entry,
     * clamped to the remaining global budget.
     * MaxPerTick = 0 means no per-entry cap.
     */
    int32 GetEffectiveBudget(int32 GlobalBudgetRemaining) const
    {
        if (MaxPerTick <= 0)
            return GlobalBudgetRemaining;
        return FMath::Min(MaxPerTick, GlobalBudgetRemaining);
    }
};
