#include "Quest/Components/SharedQuestComponent.h"
#include "Quest/Components/SharedQuestCoordinator.h"
#include "Quest/Data/SharedQuestDefinition.h"
#include "Quest/Subsystems/QuestRegistrySubsystem.h"
#include "Group/Interfaces/GroupProvider.h"
#include "GameFramework/Actor.h"

IGroupProvider* USharedQuestComponent::GetGroupProvider() const
{
    return Cast<IGroupProvider>(GetOwner());
}

USharedQuestCoordinator* USharedQuestComponent::GetCoordinator() const
{
    IGroupProvider* Provider = GetGroupProvider();
    if (!Provider) return nullptr;
    AActor* GroupActor = Provider->GetGroupActor();
    return GroupActor
        ? GroupActor->FindComponentByClass<USharedQuestCoordinator>()
        : nullptr;
}

void USharedQuestComponent::ServerRPC_AcceptQuest_Implementation(FGameplayTag QuestId)
{
    USharedQuestCoordinator* Coordinator = GetCoordinator();
    if (!Coordinator)
    {
        // No group — behave as solo quest.
        Super::ServerRPC_AcceptQuest_Implementation(QuestId);
        return;
    }

    // Route through coordinator for shared quest enrollment.
    UQuestRegistrySubsystem* Registry = GetRegistry();
    if (!Registry) return;

    Registry->GetOrLoadDefinitionAsync(QuestId,
        [WeakThis = TWeakObjectPtr<USharedQuestComponent>(this),
         QuestId](const UQuestDefinition* Def)
        {
            USharedQuestComponent* QC = WeakThis.Get();
            if (!QC) return;

            const USharedQuestDefinition* SharedDef =
                Cast<USharedQuestDefinition>(Def);

            if (!SharedDef)
            {
                // Not a shared definition — fall back to solo.
                QC->UQuestComponent::ServerRPC_AcceptQuest_Implementation(QuestId);
                return;
            }

            USharedQuestCoordinator* Coord = QC->GetCoordinator();
            if (!Coord)
            {
                QC->UQuestComponent::ServerRPC_AcceptQuest_Implementation(QuestId);
                return;
            }

            if (SharedDef->AcceptanceMode == ESharedQuestAcceptance::IndividualAccept)
            {
                // Enroll and accept individually.
                Coord->EnrollMember(QC, QuestId, EQuestMemberRole::Primary);
                QC->UQuestComponent::ServerRPC_AcceptQuest_Implementation(QuestId);
            }
            else
            {
                // LeaderAccept: this member is the leader, initiate group enrollment.
                IGroupProvider* Provider = QC->GetGroupProvider();
                if (!Provider) return;

                TArray<APlayerState*> InvitedMembers;
                Provider->GetGroupMembers(InvitedMembers);

                // Enroll leader first.
                Coord->EnrollMember(QC, QuestId, EQuestMemberRole::Primary);
                QC->UQuestComponent::ServerRPC_AcceptQuest_Implementation(QuestId);

                Coord->LeaderInitiateAccept(QuestId, SharedDef, InvitedMembers);
            }
        });
}

void USharedQuestComponent::Server_IncrementTracker(
    const FGameplayTag& QuestId,
    const FGameplayTag& TrackerKey,
    int32 Delta)
{
    USharedQuestCoordinator* Coordinator = GetCoordinator();
    if (!Coordinator)
    {
        Super::Server_IncrementTracker(QuestId, TrackerKey, Delta);
        return;
    }

    // Route through shared coordinator — it distributes back to all members.
    Coordinator->IncrementSharedTracker(QuestId, TrackerKey, Delta);
}

void USharedQuestComponent::Server_ApplyGroupSnapshot(const FQuestRuntime& SnapshotRuntime)
{
    FQuestRuntime* Existing = FindActiveQuest(SnapshotRuntime.QuestId);
    if (!Existing) return;

    // Apply de-scaled tracker values from the snapshot.
    for (const FQuestTrackerEntry& SnapshotEntry : SnapshotRuntime.Trackers)
    {
        FQuestTrackerEntry* Entry = Existing->FindTracker(SnapshotEntry.TrackerKey);
        if (Entry)
        {
            Entry->CurrentValue    = FMath::Min(SnapshotEntry.CurrentValue, Entry->EffectiveTarget);
            Entry->EffectiveTarget = SnapshotEntry.EffectiveTarget;
        }
    }

    ActiveQuests.MarkItemDirty(*Existing);
    NotifyDirty(this);
}
