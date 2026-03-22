#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ProgressionTypes.h"
#include "LevelProgressionDefinition.h"
#include "Persistence/PersistableComponent.h"
#include "LevelingComponent.generated.h"

/**
 * ULevelingComponent
 *
 * Replicated UActorComponent that manages one or more progression tracks on an Actor.
 * Each track is identified by a FGameplayTag and has its own level, XP, and definition asset.
 *
 * Server authority rules:
 *   - ApplyXP, RegisterProgression, UnregisterProgression, DeserializeFromSave are server-only.
 *   - No SetLevel / SetXP is exposed. Level is always a consequence of XP accumulation.
 *   - All external XP grants MUST go through UProgressionSubsystem::GrantXP.
 *
 * Fires intra-system delegates (OnLevelUp, OnXPChanged) for UProgressionSubsystem audit.
 * External systems MUST listen via the Event Bus (GameCoreEvent.Progression.*).
 *
 * IPersistableComponent: implements binary save/load via the Serialization System.
 * JSON helpers (SerializeToString/DeserializeFromString) are for GM tooling only —
 * never called on the save path.
 */
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API ULevelingComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()

public:
    ULevelingComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // ── Registration ──────────────────────────────────────────────────────

    // Registers a progression on this component. Checks prerequisites before registering.
    // Silent no-op if prerequisites are not met or progression is already registered.
    // Server-only.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    bool RegisterProgression(ULevelProgressionDefinition* Definition);

    // Removes a progression track. Any accumulated XP and level are discarded.
    // Server-only.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
    void UnregisterProgression(FGameplayTag ProgressionTag);

    // ── Internal XP Interface (UProgressionSubsystem only) ────────────────

    // Applies war XP to a progression, sampling the reduction policy for final XP.
    // Not exposed to gameplay code or Blueprints. Server-only.
    void ApplyXP(FGameplayTag ProgressionTag, int32 WarXP, int32 ContentLevel);

    // Returns the final XP amount applied by the most recent ApplyXP call.
    // Read by UProgressionSubsystem for audit payload construction.
    int32 GetLastAppliedXPDelta() const { return LastAppliedXPDelta; }

    // ── Read API (safe on client) ─────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetLevel(FGameplayTag ProgressionTag) const;

    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetXP(FGameplayTag ProgressionTag) const;

    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetXPToNextLevel(FGameplayTag ProgressionTag) const;

    UFUNCTION(BlueprintCallable, Category = "Progression")
    bool IsProgressionRegistered(FGameplayTag ProgressionTag) const;

    // ── Persistence — IPersistableComponent ──────────────────────────────

    virtual FName    GetPersistenceKey()  const override { return TEXT("LevelingComponent"); }
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

    // ── Delegates — INTRA-SYSTEM ONLY ────────────────────────────────────
    // External systems MUST use the Event Bus (GameCoreEvent.Progression.*)

    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionLevelUp OnLevelUp;
    // Signature: (FGameplayTag ProgressionTag, int32 NewLevel)

    UPROPERTY(BlueprintAssignable, Category = "Progression|Delegates")
    FOnProgressionXPChanged OnXPChanged;
    // Signature: (FGameplayTag ProgressionTag, int32 NewXP, int32 Delta)

private:
    UPROPERTY(Replicated)
    FProgressionLevelDataArray ProgressionData;

    // Server-only: definition assets keyed by tag. Not replicated.
    UPROPERTY()
    TMap<FGameplayTag, TObjectPtr<ULevelProgressionDefinition>> Definitions;

    // Cached result of the most recent ApplyXP call. Read by UProgressionSubsystem.
    int32 LastAppliedXPDelta = 0;

    // Dirty tracking for IPersistableComponent.
    bool     bDirty           = false;
    uint32   DirtyGeneration  = 0;

    FProgressionLevelData*       FindProgressionData(FGameplayTag Tag);
    const FProgressionLevelData* FindProgressionData(FGameplayTag Tag) const;

    void ProcessLevelUp(FProgressionLevelData& Data, ULevelProgressionDefinition* Def);
    void ProcessLevelDown(FProgressionLevelData& Data, ULevelProgressionDefinition* Def);
    void GrantPointsForLevel(ULevelProgressionDefinition* Def, int32 NewLevel);

    void NotifyDirty();
};
