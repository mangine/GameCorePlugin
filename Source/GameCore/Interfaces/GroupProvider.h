// GroupProvider.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Components/ActorComponent.h"
#include "GameFramework/PlayerState.h"
#include "GroupProvider.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class GAMECORE_API UGroupProvider : public UInterface { GENERATED_BODY() };

/**
 * Read-only interface for querying group membership state.
 * Implement on APlayerState (or delegate to UGroupProviderDelegates).
 * Consuming systems null-check the cast and fall back to solo behavior.
 */
class GAMECORE_API IGroupProvider
{
    GENERATED_BODY()
public:
    /** Number of members currently in the group, including this player. Returns 1 if ungrouped. */
    virtual int32 GetGroupSize() const = 0;

    /** True if this PlayerState is currently the group leader. */
    virtual bool IsGroupLeader() const = 0;

    /**
     * Fills OutMembers with all PlayerState members, including this player.
     * OutMembers is valid for the current frame only — do not cache the array.
     */
    virtual void GetGroupMembers(TArray<APlayerState*>& OutMembers) const = 0;

    /**
     * Returns the actor hosting USharedQuestCoordinator for this group.
     * Returns nullptr if the player is not in a group or no coordinator exists.
     */
    virtual AActor* GetGroupActor() const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * Optional delegate-backed component for APlayerState.
 * Provides a default IGroupProvider implementation via bound delegates.
 * Unbound delegates return safe solo fallbacks:
 *   GetGroupSize     → 1
 *   IsGroupLeader    → false
 *   GetGroupMembers  → [OwningPlayerState]
 *   GetGroupActor    → nullptr
 *
 * Usage: add to APlayerState, forward IGroupProvider methods to Forward*(),
 * then bind delegates from your party/group system at initialization.
 */
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UGroupProviderDelegates : public UActorComponent
{
    GENERATED_BODY()
public:
    /** Bind to return current group size. Unbound fallback: 1. */
    TDelegate<int32()> GetGroupSizeDelegate;

    /** Bind to return whether this player is the group leader. Unbound fallback: false. */
    TDelegate<bool()> IsGroupLeaderDelegate;

    /**
     * Bind to populate OutMembers with all group members including this player.
     * Unbound fallback: OutMembers contains only the owning PlayerState.
     */
    TDelegate<void(TArray<APlayerState*>&)> GetGroupMembersDelegate;

    /**
     * Bind to return the actor hosting USharedQuestCoordinator.
     * Unbound fallback: nullptr.
     */
    TDelegate<AActor*()> GetGroupActorDelegate;

    // ── IGroupProvider forwarding helpers ────────────────────────────────────
    // Called by APlayerState's IGroupProvider implementation.

    int32 ForwardGetGroupSize() const
    {
        return GetGroupSizeDelegate.IsBound()
            ? GetGroupSizeDelegate.Execute() : 1;
    }

    bool ForwardIsGroupLeader() const
    {
        return IsGroupLeaderDelegate.IsBound()
            ? IsGroupLeaderDelegate.Execute() : false;
    }

    void ForwardGetGroupMembers(TArray<APlayerState*>& OutMembers) const
    {
        if (GetGroupMembersDelegate.IsBound())
        {
            GetGroupMembersDelegate.Execute(OutMembers);
            return;
        }
        if (APlayerState* PS = GetOwner<APlayerState>())
            OutMembers.Add(PS);
    }

    AActor* ForwardGetGroupActor() const
    {
        return GetGroupActorDelegate.IsBound()
            ? GetGroupActorDelegate.Execute() : nullptr;
    }
};
