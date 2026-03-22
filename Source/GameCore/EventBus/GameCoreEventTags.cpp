#include "GameCoreEventTags.h"
#include "GameplayTagsManager.h"

namespace GameCoreEventTags
{
    FGameplayTag Alignment_Changed;

    FGameplayTag Progression_LevelUp;
    FGameplayTag Progression_XPChanged;
    FGameplayTag Progression_PointPoolChanged;

    FGameplayTag StateMachine_StateChanged;
    FGameplayTag StateMachine_TransitionBlocked;
}

/**
 * Static initializer struct that registers all native GameCore event tags
 * with UGameplayTagsManager at module load time.
 *
 * Using a static constructor here mirrors the per-system pattern described
 * in GameCore.cpp (individual systems register their own tags from their .cpp files).
 * The EventBus tags are registered here since they are the canonical channel registry.
 */
struct FGameCoreEventTagsRegistrar
{
    FGameCoreEventTagsRegistrar()
    {
        UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

        GameCoreEventTags::Alignment_Changed =
            Manager.AddNativeGameplayTag(
                TEXT("GameCoreEvent.Alignment.Changed"),
                TEXT("Fired once per UAlignmentComponent::ApplyAlignmentDeltas call when at least one axis changed."));

        GameCoreEventTags::Progression_LevelUp =
            Manager.AddNativeGameplayTag(TEXT("GameCoreEvent.Progression.LevelUp"));

        GameCoreEventTags::Progression_XPChanged =
            Manager.AddNativeGameplayTag(TEXT("GameCoreEvent.Progression.XPChanged"));

        GameCoreEventTags::Progression_PointPoolChanged =
            Manager.AddNativeGameplayTag(TEXT("GameCoreEvent.Progression.PointPoolChanged"));

        GameCoreEventTags::StateMachine_StateChanged =
            Manager.AddNativeGameplayTag(TEXT("GameCoreEvent.StateMachine.StateChanged"));

        GameCoreEventTags::StateMachine_TransitionBlocked =
            Manager.AddNativeGameplayTag(TEXT("GameCoreEvent.StateMachine.TransitionBlocked"));
    }
};

static FGameCoreEventTagsRegistrar GGameCoreEventTagsRegistrar;
