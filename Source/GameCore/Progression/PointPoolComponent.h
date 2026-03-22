#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ProgressionTypes.h"
#include "Persistence/PersistableComponent.h"
#include "PointPoolComponent.generated.h"

/**
 * UPointPoolComponent
 *
 * Standalone replicated UActorComponent that tracks named point pools on an Actor.
 * Zero knowledge of leveling — any system calls AddPoints or ConsumePoints directly.
 *
 * Server authority rules: all mutations are server-only.
 * Clients receive delta-replicated pool state via FFastArraySerializer.
 *
 * Available vs Consumed accounting:
 *   Available = total ever granted (affected by Cap)
 *   Consumed  = total ever spent
 *   Spendable = Available - Consumed
 *
 * Cap applies only to Available. Spending is cap-free.
 * Setting Consumed to zero = respec without changing lifetime grants.
 *
 * External systems MUST listen via the Event Bus (GameCoreEvent.Progression.PointPoolChanged).
 */
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UPointPoolComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()

public:
    UPointPoolComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // ── Registration ──────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void RegisterPool(FGameplayTag PoolTag, int32 Cap = 0);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    void UnregisterPool(FGameplayTag PoolTag);

    // ── Mutations (server-only) ───────────────────────────────────────────

    // Adds points to the pool. Returns EPointAddResult::PartialCap if the cap
    // was hit and some points were lost — caller should log a warning.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    EPointAddResult AddPoints(FGameplayTag PoolTag, int32 Amount);

    // Spends points from the pool. Returns false if insufficient spendable balance.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Points")
    bool ConsumePoints(FGameplayTag PoolTag, int32 Amount);

    // ── Read API (safe on client) ─────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Points")
    int32 GetSpendable(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintCallable, Category = "Points")
    int32 GetConsumed(FGameplayTag PoolTag) const;

    UFUNCTION(BlueprintCallable, Category = "Points")
    bool IsPoolRegistered(FGameplayTag PoolTag) const;

    // ── Persistence — IPersistableComponent ──────────────────────────────

    virtual FName    GetPersistenceKey()  const override { return TEXT("PointPoolComponent"); }
    virtual uint32   GetSchemaVersion()   const override { return 1; }
    virtual void     Serialize_Save(FArchive& Ar) override;
    virtual void     Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void     ClearIfSaved(uint32 FlushedGeneration) override;
    virtual bool     IsDirty() const override { return bDirty; }

    // Debug/tooling helpers — never called on the save path.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    FString SerializeToString() const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence|Debug")
    void DeserializeFromString(const FString& Data);

    // ── Delegate — INTRA-SYSTEM ONLY ─────────────────────────────────────
    // External systems MUST use GameCoreEvent.Progression.PointPoolChanged

    UPROPERTY(BlueprintAssignable, Category = "Points|Delegates")
    FOnPointPoolChanged OnPoolChanged;
    // Signature: (FGameplayTag PoolTag, int32 NewSpendable, int32 Delta)

private:
    UPROPERTY(Replicated)
    FPointPoolDataArray PoolData;

    FPointPoolData*       FindPool(FGameplayTag Tag);
    const FPointPoolData* FindPool(FGameplayTag Tag) const;

    // Fires the intra-system delegate and the Event Bus broadcast after any mutation.
    void NotifyPoolChanged(FGameplayTag PoolTag, int32 NewSpendable, int32 Delta);

    // Dirty tracking for IPersistableComponent.
    bool   bDirty          = false;
    uint32 DirtyGeneration = 0;
    void   NotifyDirty();
};
