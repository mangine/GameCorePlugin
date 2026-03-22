#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Quest/Enums/QuestEnums.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "QuestRegistrySubsystem.generated.h"

class UQuestDefinition;

UCLASS()
class GAMECORE_API UQuestRegistrySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ── Definition Lookup ────────────────────────────────────────────────────

    /** Returns loaded definition synchronously, or nullptr if not yet loaded. Does NOT trigger a load. */
    const UQuestDefinition* GetDefinition(const FGameplayTag& QuestId) const;

    /**
     * Async load. If already loaded, fires callback synchronously in the same frame.
     * Callers must handle both sync and async completion paths.
     */
    void GetOrLoadDefinitionAsync(
        const FGameplayTag& QuestId,
        TFunction<void(const UQuestDefinition*)> OnLoaded);

    // ── Reference Counting ────────────────────────────────────────────────────

    /** Called by UQuestComponent on quest accept. Prevents unload while active. */
    void AddReference(const FGameplayTag& QuestId);

    /** Called by UQuestComponent on complete/fail/abandon. Unloads when RefCount reaches zero. */
    void ReleaseReference(const FGameplayTag& QuestId);

    // ── Iteration ─────────────────────────────────────────────────────────────

    /**
     * Iterates all resident quest definitions (those currently in memory).
     * Definitions not yet loaded are skipped — UQuestComponent picks them up lazily.
     */
    void IterateAllDefinitions(
        TFunctionRef<void(const FGameplayTag&, const UQuestDefinition*)> Visitor) const;

    /** Returns all known quest asset IDs as discovered at Initialize. */
    const TArray<FPrimaryAssetId>& GetAllQuestAssetIds() const
    { return AllQuestAssetIds; }

    // ── Cadence Clock ─────────────────────────────────────────────────────────

    int64 GetLastDailyResetTimestamp()  const { return LastDailyResetTimestamp; }
    int64 GetLastWeeklyResetTimestamp() const { return LastWeeklyResetTimestamp; }

private:

    struct FQuestLoadState
    {
        TObjectPtr<const UQuestDefinition>                   Definition    = nullptr;
        int32                                                RefCount      = 0;
        TSharedPtr<FStreamableHandle>                        LoadHandle;
        TArray<TFunction<void(const UQuestDefinition*)>>     PendingCallbacks;
    };

    TMap<FGameplayTag, FQuestLoadState> LoadedDefinitions;
    TArray<FPrimaryAssetId>             AllQuestAssetIds;
    /** Pre-built leaf-name map for O(1) tag-to-asset path resolution. */
    TMap<FName, FPrimaryAssetId>        QuestAssetIdByLeafName;

    int64        LastDailyResetTimestamp  = 0;
    int64        LastWeeklyResetTimestamp = 0;
    FTimerHandle CadenceCheckTimer;

    FSoftObjectPath ResolveQuestPath(const FGameplayTag& QuestId) const;

    void TickCadenceCheck();
    void ComputeResetTimestamps();
    void OnDailyReset();
    void OnWeeklyReset();
    void FlushCadenceResetsForAllPlayers(EQuestResetCadence Cadence);

    static int64 ComputeLastDailyReset(int64 NowTs);
    static int64 ComputeLastWeeklyReset(int64 NowTs);
};
