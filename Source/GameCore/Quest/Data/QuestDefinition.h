#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Quest/Enums/QuestEnums.h"
#include "Quest/Data/QuestDisplayData.h"
#include "Quest/Data/QuestStageDefinition.h"
#include "StateMachine/StateMachineAsset.h"
#include "Requirements/RequirementList.h"
#include "QuestDefinition.generated.h"

class ULootTable;

UCLASS(BlueprintType)
class GAMECORE_API UQuestDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // ── Identity ────────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Id"))
    FGameplayTag QuestId;

    /**
     * Tag added to CompletedQuestTags on permanent close.
     * SingleAttempt: added on complete AND fail.
     * RetryUntilComplete: added on complete only.
     * RetryAndAssist / Evergreen: never added.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest",
              meta=(Categories="Quest.Completed"))
    FGameplayTag QuestCompletedTag;

    // ── Live-ops ─────────────────────────────────────────────────────────────

    /** Kill switch. Disabled quests are excluded from candidate unlock lists. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    bool bEnabled = true;

    // ── Lifecycle & Rules ────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestLifecycle Lifecycle = EQuestLifecycle::RetryUntilComplete;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestCheckAuthority CheckAuthority = EQuestCheckAuthority::ClientValidated;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    EQuestResetCadence ResetCadence = EQuestResetCadence::None;

    /** Unix timestamp (int64). 0 = no expiry. Only meaningful for EventBound cadence. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rules")
    int64 ExpiryTimestamp = 0;

    // ── Stage Graph ──────────────────────────────────────────────────────────

    /** UStateMachineAsset drives stage transition logic. UStateMachineComponent is NOT added to APlayerState. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Stages")
    TObjectPtr<UStateMachineAsset> StageGraph;

    /** Stage definitions — one per state node in StageGraph. Looked up by StageTag at runtime. */
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category="Quest|Stages")
    TArray<TObjectPtr<UQuestStageDefinition>> Stages;

    // ── Requirements ─────────────────────────────────────────────────────────

    /** Requirements evaluated reactively (RegisterWatch) for unlock detection and imperatively at login. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Requirements")
    TObjectPtr<URequirementList> UnlockRequirements;

    // ── Rewards ──────────────────────────────────────────────────────────────

    /** Soft reference — loaded and granted by the reward system on Quest.Completed. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> FirstTimeRewardTable;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Rewards")
    TSoftObjectPtr<ULootTable> RepeatingRewardTable;

    // ── Categorisation & Display ──────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display",
              meta=(Categories="Quest.Marker"))
    FGameplayTag QuestMarkerTag;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FGameplayTagContainer QuestCategories;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Display")
    FQuestDisplayData Display;

    // ── API ───────────────────────────────────────────────────────────────────

    const UQuestStageDefinition* FindStage(const FGameplayTag& StageTag) const
    {
        const TObjectPtr<UQuestStageDefinition>* Found =
            Stages.FindByPredicate(
                [&](const TObjectPtr<UQuestStageDefinition>& S)
                { return S && S->StageTag == StageTag; });
        return Found ? Found->Get() : nullptr;
    }

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("QuestDefinition"), GetFName());
    }

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
