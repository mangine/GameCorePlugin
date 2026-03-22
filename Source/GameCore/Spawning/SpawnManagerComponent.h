#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Spawning/SpawnEntry.h"
#include "Spawning/SpawnPointConfig.h"
#include "SpawnManagerComponent.generated.h"

class ULootTable;

/**
 * Server-only UActorComponent. Placed on a spawn anchor actor.
 * Drives the full spawn lifecycle: async class loading, flow-timer scheduling,
 * per-entry requirement evaluation, spawn-point resolution, and live-instance tracking.
 */
UCLASS(ClassGroup = "GameCore", meta = (BlueprintSpawnableComponent))
class GAMECORE_API USpawnManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USpawnManagerComponent();

    // ── Entries ───────────────────────────────────────────────────────────────

    /** Entity types this manager keeps alive. Entry order = implicit spawn priority. */
    UPROPERTY(EditAnywhere, Category = "Spawning")
    TArray<FSpawnEntry> SpawnEntries;

    // ── Flow Control ──────────────────────────────────────────────────────────

    /** Base seconds between flow ticks. Always >= 10 s at runtime. */
    UPROPERTY(EditAnywhere, Category = "Spawning|Flow", meta = (ClampMin = 10.f))
    float BaseFlowInterval = 30.f;

    /** Maximum total spawns across all entries per flow tick. */
    UPROPERTY(EditAnywhere, Category = "Spawning|Flow", meta = (ClampMin = 1))
    int32 GlobalFlowCount = 1;

    // ── Spawn Point ───────────────────────────────────────────────────────────

    /**
     * Instanced strategy object that resolves world-space spawn transforms.
     * If null at BeginPlay, defaults to USpawnPointConfig_RadiusRandom (Radius=500).
     */
    UPROPERTY(EditAnywhere, Instanced, Category = "Spawning|SpawnPoint")
    TObjectPtr<USpawnPointConfig> SpawnPointConfig;

    // ── Player Multiplier ─────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, Category = "Spawning|PlayerMultiplier")
    bool bScaleByNearbyPlayers = false;

    UPROPERTY(EditAnywhere, Category = "Spawning|PlayerMultiplier",
        meta = (EditCondition = "bScaleByNearbyPlayers", ClampMin = 100.f))
    float PlayerScanRadius = 2000.f;

    /** Player count at which flow interval reaches MinFlowInterval. */
    UPROPERTY(EditAnywhere, Category = "Spawning|PlayerMultiplier",
        meta = (EditCondition = "bScaleByNearbyPlayers", ClampMin = 1))
    int32 PlayerCountForMinInterval = 5;

    /** Minimum achievable flow interval. Always >= 10 s at runtime. */
    UPROPERTY(EditAnywhere, Category = "Spawning|PlayerMultiplier",
        meta = (EditCondition = "bScaleByNearbyPlayers", ClampMin = 10.f))
    float MinFlowInterval = 10.f;

    /**
     * Injected by the game module. Called once per flow tick when bScaleByNearbyPlayers is true.
     * Signature: int32(FVector Location, float Radius)
     * If unbound, interval scaling is skipped and a warning is logged once.
     */
    TFunction<int32(FVector, float)> OnCountNearbyPlayers;

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * Returns the soft loot table override for the given actor class.
     * Called by ISpawnableEntity implementors from OnSpawnedByManager.
     */
    UFUNCTION(BlueprintCallable, Category = "Spawning")
    TSoftObjectPtr<ULootTable> GetLootTableOverrideForClass(
        TSubclassOf<AActor> ActorClass) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    void OnFlowTimerExpired();
    void ScheduleNextFlowTick(int32 NearbyPlayers);
    float ComputeNextInterval(int32 NearbyPlayers) const;

    FTimerHandle FlowTimerHandle;

    AActor* TrySpawnForEntry(FSpawnEntry& Entry);

    UFUNCTION()
    void OnSpawnedActorDestroyed(AActor* DestroyedActor);

    void RequestAsyncClassLoads();
    void OnClassesLoaded();

    int32 GetNearbyPlayerCount() const;

    mutable bool bDelegateWarningLogged = false;
};
