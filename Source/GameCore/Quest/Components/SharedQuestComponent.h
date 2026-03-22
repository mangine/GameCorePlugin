#pragma once

#include "CoreMinimal.h"
#include "Quest/Components/QuestComponent.h"
#include "SharedQuestComponent.generated.h"

class USharedQuestCoordinator;
class IGroupProvider;

/**
 * Inherits UQuestComponent. Overrides ServerRPC_AcceptQuest and Server_IncrementTracker
 * to route through USharedQuestCoordinator when a group provider is available.
 * Falls back to base behavior when no IGroupProvider is present on the owner.
 * Drop-in replacement — adds no overhead when there is no active group.
 */
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API USharedQuestComponent : public UQuestComponent
{
    GENERATED_BODY()
public:

    /** Called by USharedQuestCoordinator to apply a de-scaled snapshot when this member leaves a shared quest. */
    void Server_ApplyGroupSnapshot(const FQuestRuntime& SnapshotRuntime);

protected:

    virtual void ServerRPC_AcceptQuest_Implementation(FGameplayTag QuestId) override;

    virtual void Server_IncrementTracker(
        const FGameplayTag& QuestId,
        const FGameplayTag& TrackerKey,
        int32 Delta = 1) override;

private:

    IGroupProvider* GetGroupProvider() const;
    USharedQuestCoordinator* GetCoordinator() const;
};
