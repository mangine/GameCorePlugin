#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quest/Enums/QuestEnums.h"
#include "GameFramework/PlayerState.h"
#include "QuestEventPayloads.generated.h"

class ULootTable;

// ---------------------------------------------------------------------------
// Gameplay Tag declarations (defined in QuestEventPayloads.cpp)
// ---------------------------------------------------------------------------

GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_Started);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_Completed);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_Failed);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_Abandoned);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_BecameAvailable);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_StageStarted);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_StageCompleted);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_StageFailed);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_TrackerUpdated);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_DailyReset);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_WeeklyReset);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_GroupInvite);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameCoreEvent_Quest_MemberLeft);

GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_RequirementEvent_Quest_TrackerUpdated);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_RequirementEvent_Quest_StageChanged);
GAMECORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_RequirementEvent_Quest_Completed);

// ---------------------------------------------------------------------------
// Payload structs
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestStartedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    UPROPERTY() EQuestMemberRole MemberRole = EQuestMemberRole::Primary;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestCompletedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    UPROPERTY() EQuestMemberRole MemberRole = EQuestMemberRole::Primary;
    UPROPERTY() TSoftObjectPtr<ULootTable> RewardTable;
    UPROPERTY() bool bIsHelperRun = false;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestFailedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    UPROPERTY() bool bPermanentlyClosed = false;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestAbandonedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestBecameAvailablePayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestStageChangedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() FGameplayTag StageTag;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
    UPROPERTY() FText ObjectiveText;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestTrackerUpdatedPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() FGameplayTag TrackerKey;
    UPROPERTY() int32 OldValue = 0;
    UPROPERTY() int32 NewValue = 0;
    UPROPERTY() int32 EffectiveTarget = 1;
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestResetPayload
{
    GENERATED_BODY()
    UPROPERTY() EQuestResetCadence Cadence = EQuestResetCadence::Daily;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestGroupInvitePayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TArray<TWeakObjectPtr<APlayerState>> InvitedMembers;
    UPROPERTY() float GraceWindowSeconds = 10.0f;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FQuestMemberLeftPayload
{
    GENERATED_BODY()
    UPROPERTY() FGameplayTag QuestId;
    UPROPERTY() TWeakObjectPtr<APlayerState> LeavingMember;
    UPROPERTY() int32 RemainingMemberCount = 0;
};
