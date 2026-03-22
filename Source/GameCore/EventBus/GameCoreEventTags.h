#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/**
 * Native FGameplayTag handle declarations for all GameCore event channels.
 *
 * Handles are cached at module startup via UGameplayTagsManager::AddNativeGameplayTag.
 * Zero-cost lookup at broadcast sites — no string lookup at runtime.
 *
 * Channel tags follow GameCoreEvent.<Module>.<Event> naming convention.
 * Tags are registered in DefaultGameplayTags.ini inside the GameCore module.
 */
namespace GameCoreEventTags
{
    // ── Alignment ─────────────────────────────────────────────────────────────

    /** Fired once per UAlignmentComponent::ApplyAlignmentDeltas call when at least one axis changed. */
    GAMECORE_API extern FGameplayTag Alignment_Changed;

    // ── Progression ──────────────────────────────────────────────────────────

    GAMECORE_API extern FGameplayTag Progression_LevelUp;
    GAMECORE_API extern FGameplayTag Progression_XPChanged;
    GAMECORE_API extern FGameplayTag Progression_PointPoolChanged;

    // ── State Machine ─────────────────────────────────────────────────────────

    GAMECORE_API extern FGameplayTag StateMachine_StateChanged;
    GAMECORE_API extern FGameplayTag StateMachine_TransitionBlocked;
}
