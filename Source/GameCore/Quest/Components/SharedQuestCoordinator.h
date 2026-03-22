#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Quest/Enums/QuestEnums.h"
#include "Quest/Runtime/QuestRuntime.h"
#include "SharedQuestCoordinator.generated.h"

class USharedQuestComponent;
class USharedQuestDefinition;
class APlayerState;

/**
 * Owns the shared tracker truth for formally shared group quests.
 * Lives on the group/party actor (which must implement IGroupProvider).
 * Individual USharedQuestComponent instances push increments through this coordinator.
 * Server-only authority.
 */
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API USharedQuestCoordinator : public UActorComponent
{
    GENERATED_BODY()
public:

    /**
     * Bind to integrate with your group/party system.
     * Called when a LeaderAccept quest needs a group enrollment handshake.
     * If unbound: coordinator auto-enrolls all InvitedMembers immediately.
     */
    TDelegate<void(
        const FGameplayTag& /*QuestId*/,
        const TArray<APlayerState*>& /*InvitedMembers*/,
        float /*GraceSeconds*/,
        TFunction<void(const TArray<APlayerState*>&)> /*OnEnrollmentResolved*/
    )> OnRequestGroupEnrollment;

    // ── Enrollment ────────────────────────────────────────────────────────────

    void EnrollMember(
        USharedQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId,
        EQuestMemberRole Role = EQuestMemberRole::Primary);

    void RemoveMember(
        USharedQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId);

    // ── Tracker ────────────────────────────────────────────────────────────────

    void IncrementSharedTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1);

    int32 GetEnrolledMemberCount(const FGameplayTag& QuestId) const;

    // ── LeaderAccept ──────────────────────────────────────────────────────────

    void LeaderInitiateAccept(
        const FGameplayTag& QuestId,
        const USharedQuestDefinition* Def,
        const TArray<APlayerState*>& InvitedMembers);

private:

    struct FSharedQuestState
    {
        FGameplayTag QuestId;
        TArray<TWeakObjectPtr<USharedQuestComponent>> Members;
        TMap<FGameplayTag, int32> SharedCounters;
        TMap<FGameplayTag, int32> EffectiveTargets;
        TObjectPtr<const USharedQuestDefinition> Definition;
    };

    TMap<FGameplayTag, FSharedQuestState> ActiveSharedQuests;

    void RecalculateEffectiveTargets(FSharedQuestState& State);
    void DistributeTrackerUpdate(
        FSharedQuestState& State,
        const FGameplayTag& TrackerKey,
        int32 NewSharedValue);
    FQuestRuntime BuildDeScaledSnapshot(
        const FSharedQuestState& State,
        USharedQuestComponent* LeavingMember) const;
    void OnMemberPersonalFail(
        USharedQuestComponent* MemberQuestComp,
        const FGameplayTag& QuestId);
};
