#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Quest/Runtime/QuestRuntime.h"
#include "Quest/Enums/QuestEnums.h"
#include "Quest/Data/QuestConfigDataAsset.h"
#include "Persistence/PersistableComponent.h"
#include "EventBus/GameCoreEventWatcher.h"
#include "Requirements/RequirementContext.h"
#include "QuestComponent.generated.h"

class UQuestDefinition;
class UQuestStageDefinition;
class UQuestRegistrySubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnQuestEventDelegate, FGameplayTag, QuestId, EQuestEventType, EventType);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnTrackerUpdatedDelegate,
    FGameplayTag, QuestId, FGameplayTag, TrackerKey, int32, NewValue);

UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UQuestComponent
    : public UActorComponent
    , public IPersistableComponent
{
    GENERATED_BODY()
public:

    // ── Configuration ────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, Category="Quest")
    TSoftObjectPtr<UQuestConfigDataAsset> QuestConfig;

    // ── Replicated State ─────────────────────────────────────────────────────

    UPROPERTY(Replicated)
    FQuestRuntimeArray ActiveQuests;

    /** Tags for permanently closed quests (complete or SingleAttempt fail). */
    UPROPERTY(ReplicatedUsing=OnRep_CompletedQuestTags)
    FGameplayTagContainer CompletedQuestTags;

    UFUNCTION()
    void OnRep_CompletedQuestTags();

    // ── Delegates ─────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category="Quest")
    FOnQuestEventDelegate OnQuestEvent;

    UPROPERTY(BlueprintAssignable, Category="Quest")
    FOnTrackerUpdatedDelegate OnTrackerUpdated;

    // ── Public Server API ────────────────────────────────────────────────────

    /** Entry point for all tracker progress. Called by external bridge components. Server-only. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    virtual void Server_IncrementTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    void Server_ForceCompleteQuest(const FGameplayTag& QuestId);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Quest")
    void Server_ForceFailQuest(const FGameplayTag& QuestId);

    /** Called by UQuestRegistrySubsystem on Daily/Weekly reset. */
    void FlushCadenceResets(EQuestResetCadence Cadence);

    FQuestRuntime*       FindActiveQuest(const FGameplayTag& QuestId);
    const FQuestRuntime* FindActiveQuest(const FGameplayTag& QuestId) const;

    int32 GetMaxActiveQuests() const;

    // ── RPCs ───────────────────────────────────────────────────────────────────

    UFUNCTION(Server, Reliable)
    virtual void ServerRPC_AcceptQuest(FGameplayTag QuestId);

    UFUNCTION(Server, Reliable)
    void ServerRPC_AbandonQuest(FGameplayTag QuestId);

    UFUNCTION(Server, Reliable)
    void ServerRPC_RequestValidation(FGameplayTag QuestId, FGameplayTag StageTag);

    UFUNCTION(Client, Reliable)
    void ClientRPC_NotifyQuestEvent(FGameplayTag QuestId, EQuestEventType EventType);

    UFUNCTION(Client, Reliable)
    void ClientRPC_NotifyValidationRejected(
        FGameplayTag QuestId, FGameplayTag StageTag, EQuestRejectionReason Reason);

    // ── IPersistableComponent ─────────────────────────────────────────────────

    virtual FName  GetPersistenceKey()  const override { return TEXT("QuestComponent"); }
    virtual uint32 GetSchemaVersion()   const override { return 1; }
    virtual void   Serialize_Save(FArchive& Ar) override;
    virtual void   Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void   Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion) override {}

    // ── Lifecycle ──────────────────────────────────────────────────────────────

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

    /** Unlock watcher handles keyed by QuestId. */
    TMap<FGameplayTag, FEventWatchHandle> UnlockWatcherHandles;

    UPROPERTY()
    TObjectPtr<UQuestConfigDataAsset> LoadedConfig;

    // ── Internal flow (server-side only) ─────────────────────────────────────

    virtual void Internal_CompleteQuest(FQuestRuntime& Runtime,
                                        const UQuestDefinition* Def);
    virtual void Internal_FailQuest(FQuestRuntime& Runtime,
                                    const UQuestDefinition* Def);
    void Internal_AdvanceStage(FQuestRuntime& Runtime,
                               const UQuestDefinition* Def,
                               const FGameplayTag& NewStageTag);
    void Internal_InitTrackers(FQuestRuntime& Runtime,
                               const UQuestStageDefinition* StageDef,
                               int32 GroupSize = 1);

    void EvaluateCompletionRequirementsNow(const FGameplayTag& QuestId);

    FRequirementContext BuildRequirementContext() const;

    FGameplayTag ResolveNextStage(const FQuestRuntime& Runtime,
                                  const UQuestDefinition* Def) const;

    void ValidateActiveQuestsOnLogin();
    void RegisterUnlockWatchers();
    void RegisterUnlockWatcherForQuest(const FGameplayTag& QuestId,
                                        const UQuestDefinition* Def);
    void RegisterClientValidatedCompletionWatcher(const FQuestRuntime& Runtime);
    void RegisterClientValidatedCompletionWatchers();

    bool ShouldWatchUnlock(const FGameplayTag& QuestId,
                           const UQuestDefinition* Def) const;

    UQuestRegistrySubsystem* GetRegistry() const;
};
