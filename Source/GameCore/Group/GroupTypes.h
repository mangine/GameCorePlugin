// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "GameFramework/PlayerState.h"
#include "GroupTypes.generated.h"

class AGroupActor;
class ARaidActor;

// ─── Log category ─────────────────────────────────────────────────────────────
GAMECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogGroup, Log, All);

// ─── Native tag namespace ─────────────────────────────────────────────────────
namespace GameCoreGroupTags
{
    GAMECORE_API extern FGameplayTag InviteReceived;
    GAMECORE_API extern FGameplayTag InviteExpired;
    GAMECORE_API extern FGameplayTag InviteDeclined;
    GAMECORE_API extern FGameplayTag Formed;
    GAMECORE_API extern FGameplayTag MemberJoined;
    GAMECORE_API extern FGameplayTag MemberLeft;
    GAMECORE_API extern FGameplayTag MemberKicked;
    GAMECORE_API extern FGameplayTag MemberDisconnected;
    GAMECORE_API extern FGameplayTag MemberReconnected;
    GAMECORE_API extern FGameplayTag LeaderChanged;
    GAMECORE_API extern FGameplayTag Disbanded;
    GAMECORE_API extern FGameplayTag Raid_GroupJoined;
    GAMECORE_API extern FGameplayTag Raid_GroupLeft;
    GAMECORE_API extern FGameplayTag Raid_LeaderChanged;
    GAMECORE_API extern FGameplayTag Raid_Disbanded;
}

// =============================================================================
// EGroupRaidRole
// =============================================================================

UENUM(BlueprintType)
enum class EGroupRaidRole : uint8
{
    None        UMETA(DisplayName="None"),        // Not in a raid.
    Member      UMETA(DisplayName="Member"),      // Regular group member inside a raid.
    GroupLeader UMETA(DisplayName="GroupLeader"), // Leader of their own group within a raid.
    Assistant   UMETA(DisplayName="Assistant"),   // Raid assistant: can kick, cannot manage leadership.
    RaidLeader  UMETA(DisplayName="RaidLeader"),  // Single primary raid authority.
};

// =============================================================================
// UMemberSharedData
// =============================================================================

/**
 * Abstract base class for developer-defined per-member data shared with group or raid members.
 * Subclass in your game module. Implement GatherFromPlayerState and NetSerialize.
 *
 * IMPORTANT: Shared data objects are NewObject'd with the AGroupActor as outer.
 * They are garbage-collected when the group is disbanded.
 * Do not hold external raw pointers to these objects.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class GAMECORE_API UMemberSharedData : public UObject
{
    GENERATED_BODY()
public:
    // Called server-side on the heartbeat interval and whenever IsDirty() returns true.
    // Populate your UPROPERTY fields here from the owning PlayerState.
    virtual void GatherFromPlayerState(APlayerState* PS) {}

    // Implement in subclass to serialize/deserialize replicated fields.
    virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
    {
        bOutSuccess = true;
        return true;
    }

    void MarkDirty()  { bDirty = true; }
    void ClearDirty() { bDirty = false; }
    bool IsDirty() const { return bDirty; }

private:
    bool bDirty = false;
};

// =============================================================================
// FGroupMemberEntry
// =============================================================================

USTRUCT(BlueprintType)
struct GAMECORE_API FGroupMemberEntry
{
    GENERATED_BODY()

    // The PlayerState of this member. Valid on server; replicated as actor reference to client.
    // May be null for a disconnected member still within the grace window.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<APlayerState> MemberPlayerState = nullptr;

    // Stable UniqueNetId string — used to match a reconnecting player to their slot.
    UPROPERTY()
    FString UniqueNetIdString;

    // Stable display order index within the group (0-based).
    UPROPERTY(BlueprintReadOnly)
    uint8 SlotIndex = 0;

    // True if this player is the group leader.
    UPROPERTY(BlueprintReadOnly)
    bool bIsGroupLeader = false;

    // True if this member is currently disconnected within the grace window.
    UPROPERTY(BlueprintReadOnly)
    bool bDisconnected = false;

    // Raid role for this member. None if not in a raid.
    UPROPERTY(BlueprintReadOnly)
    EGroupRaidRole RaidRole = EGroupRaidRole::None;

    // Developer-defined data shared with members of the same group.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UMemberSharedData> GroupSharedData = nullptr;

    // Developer-defined data shared with members of OTHER groups in the same raid.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UMemberSharedData> RaidSharedData = nullptr;
};

// =============================================================================
// FGroupMemberView
// =============================================================================

/**
 * Full group/raid snapshot held on each APlayerState and replicated COND_OwnerOnly.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FGroupMemberView
{
    GENERATED_BODY()

    // All members in the player's own group, including themselves.
    UPROPERTY(BlueprintReadOnly)
    TArray<FGroupMemberEntry> Members;

    // True if this group is currently part of a raid.
    UPROPERTY(BlueprintReadOnly)
    bool bIsInRaid = false;

    // This group's index within the raid (0-based).
    UPROPERTY(BlueprintReadOnly)
    uint8 GroupIndexInRaid = 0;

    // Entries for members of OTHER groups in the raid.
    // GroupSharedData is null on all entries — only RaidSharedData is populated.
    UPROPERTY(BlueprintReadOnly)
    TArray<FGroupMemberEntry> RaidMembers;
};

// =============================================================================
// FPendingGroupInvite (server-only, not a USTRUCT)
// =============================================================================

struct FPendingGroupInvite
{
    TWeakObjectPtr<APlayerState> InviterPS;
    TWeakObjectPtr<APlayerState> TargetPS;
    float ExpiryTime = 0.f;
    bool bIsRaidInvite = false;
};

// =============================================================================
// FDisconnectGraceEntry (server-only, not a USTRUCT)
// =============================================================================

struct FDisconnectGraceEntry
{
    TWeakObjectPtr<AGroupActor> Group;
    uint8 SlotIndex = 0;
    FString UniqueNetIdString;
    float ExpiryTime = 0.f;
    FTimerHandle TimerHandle;
};

// =============================================================================
// UGroupConfigDataAsset
// =============================================================================

UCLASS(BlueprintType)
class GAMECORE_API UGroupConfigDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:

    // Maximum number of members per group (inclusive). Minimum valid value is 2.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Group", meta=(ClampMin=2))
    int32 MaxGroupSize = 5;

    // Maximum number of groups per raid (inclusive). Minimum valid value is 2.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Raid", meta=(ClampMin=2))
    int32 MaxGroupsPerRaid = 4;

    // Seconds before an unanswered invite is automatically cancelled by the server.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Invites", meta=(ClampMin=5.f))
    float InviteExpirySeconds = 30.f;

    // Seconds a disconnected member's slot is held before they are removed.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Disconnect", meta=(ClampMin=0.f))
    float DisconnectGraceSeconds = 60.f;

    // How often (in seconds) shared data is gathered from each PlayerState even if not dirty.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SharedData", meta=(ClampMin=0.1f))
    float SharedDataHeartbeatInterval = 1.f;

    // UMemberSharedData subclass instantiated per slot for own-group visibility.
    // Null = no shared data sent within a group.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SharedData")
    TSubclassOf<UMemberSharedData> GroupSharedDataClass;

    // UMemberSharedData subclass instantiated per slot for cross-group raid visibility.
    // Null = no shared data sent to other groups in a raid.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SharedData")
    TSubclassOf<UMemberSharedData> RaidSharedDataClass;
};

// =============================================================================
// GMS Event Message Structs
// =============================================================================

USTRUCT(BlueprintType)
struct GAMECORE_API FGroupInviteMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<APlayerState> InviterPS;
    UPROPERTY() TWeakObjectPtr<APlayerState> TargetPS;
    UPROPERTY() bool bIsRaidInvite = false;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FGroupMemberChangedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<APlayerState> AffectedPS;
    UPROPERTY() TWeakObjectPtr<AGroupActor>  GroupActor;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FGroupLeaderChangedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<AGroupActor>  GroupActor;
    UPROPERTY() TWeakObjectPtr<APlayerState> NewLeaderPS;
    UPROPERTY() TWeakObjectPtr<APlayerState> OldLeaderPS;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FGroupDisbandedMessage
{
    GENERATED_BODY()
    // GroupActor is about to be destroyed — do not hold beyond the callback frame.
    UPROPERTY() TWeakObjectPtr<AGroupActor> GroupActor;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FRaidGroupChangedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<ARaidActor>  RaidActor;
    UPROPERTY() TWeakObjectPtr<AGroupActor> GroupActor;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FRaidLeaderChangedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<ARaidActor>   RaidActor;
    UPROPERTY() TWeakObjectPtr<APlayerState> NewRaidLeaderPS;
    UPROPERTY() TWeakObjectPtr<APlayerState> OldRaidLeaderPS;
};

USTRUCT(BlueprintType)
struct GAMECORE_API FRaidDisbandedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<ARaidActor> RaidActor;
};
