# GroupTypes

**File:** `Group/GroupTypes.h`
**Authority:** Shared — types used on both server and client.

Contains all enums, structs, the `UMemberSharedData` base class, `UGroupConfigDataAsset`, and all GMS event message structs for the Group System.

---

## EGroupRaidRole

Role of a player within a raid context. Stored in `FGroupMemberEntry` and replicated as part of `FGroupMemberView`.

```cpp
UENUM(BlueprintType)
enum class EGroupRaidRole : uint8
{
    None        UMETA(DisplayName="None"),        // Not in a raid.
    Member      UMETA(DisplayName="Member"),      // Regular group member inside a raid.
    GroupLeader UMETA(DisplayName="GroupLeader"), // Leader of their own group within a raid.
    Assistant   UMETA(DisplayName="Assistant"),   // Raid assistant: can kick, cannot manage leadership.
    RaidLeader  UMETA(DisplayName="RaidLeader"),  // Single primary raid authority.
};
```

---

## UMemberSharedData

Abstract base class for developer-defined per-member data shared with group or raid members. Two instances are held per member slot: one for within-group visibility (`GroupSharedData`) and one for cross-group raid visibility (`RaidSharedData`).

Subclass in your game module. Implement `GatherFromPlayerState` to populate fields and `NetSerialize` to replicate them.

```cpp
UCLASS(Abstract, BlueprintType, Blueprintable)
class GAMECORE_API UMemberSharedData : public UObject
{
    GENERATED_BODY()
public:
    // Called server-side on the heartbeat interval and whenever IsDirty() returns true.
    // Populate your UPROPERTY fields here from the owning PlayerState.
    virtual void GatherFromPlayerState(APlayerState* PS) {}

    // Implement in subclass to serialize/deserialize replicated fields.
    // Called by UGroupComponent when building the net update for FGroupMemberEntry.
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
```

**Important notes:**
- `GatherFromPlayerState` runs **server-only**. Never call it from a client code path.
- Subclasses must be non-abstract `UCLASS()` to be assignable in `UGroupConfigDataAsset`.
- `NetSerialize` must be symmetric (same field order for read and write).
- Do not store raw pointers to `UObject`s that may not be network-addressable. Only replicate value types and net-safe soft references.
- Shared data objects are `NewObject`'d with the `AGroupActor` as outer. They are garbage-collected when the group is disbanded. Do not hold external raw pointers to them.

---

## FGroupMemberEntry

One slot in a group. Held in `FGroupMemberView::Members` and replicated to the owning player.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGroupMemberEntry
{
    GENERATED_BODY()

    // The PlayerState of this member. Valid on server; replicated as actor reference to client.
    // May be null for a disconnected member still within the grace window.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<APlayerState> MemberPlayerState = nullptr;

    // Stable UniqueNetId string — used to match a reconnecting player to their slot.
    // Stored as FString because FUniqueNetIdRepl is not trivially net-serializable
    // across all online subsystems.
    UPROPERTY()
    FString UniqueNetIdString;

    // Stable display order index within the group (0-based). Does not change
    // when other members leave. Assigned at join time from the lowest free slot.
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
    // Instance type matches UGroupConfigDataAsset::GroupSharedDataClass.
    // Null if GroupSharedDataClass is not configured.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UMemberSharedData> GroupSharedData = nullptr;

    // Developer-defined data shared with members of OTHER groups in the same raid.
    // Instance type matches UGroupConfigDataAsset::RaidSharedDataClass.
    // Null if RaidSharedDataClass is not configured, or if not in a raid.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UMemberSharedData> RaidSharedData = nullptr;
};
```

---

## FGroupMemberView

Full group/raid snapshot held on each `APlayerState` and replicated `COND_OwnerOnly`. This is everything the UI needs.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGroupMemberView
{
    GENERATED_BODY()

    // All members in the player's own group, including themselves. Max = MaxGroupSize.
    UPROPERTY(BlueprintReadOnly)
    TArray<FGroupMemberEntry> Members;

    // True if this group is currently part of a raid.
    UPROPERTY(BlueprintReadOnly)
    bool bIsInRaid = false;

    // This group's index within the raid (0-based). Meaningless if bIsInRaid is false.
    UPROPERTY(BlueprintReadOnly)
    uint8 GroupIndexInRaid = 0;

    // Entries for members of OTHER groups in the raid.
    // Each entry has GroupSharedData = null — full group data of another group
    // is never sent cross-group. Only RaidSharedData is populated.
    // Empty if bIsInRaid is false or if RaidSharedDataClass is null.
    UPROPERTY(BlueprintReadOnly)
    TArray<FGroupMemberEntry> RaidMembers;
};
```

---

## FPendingGroupInvite

Held server-side in `UGroupSubsystem` until accepted, declined, or expired. Not a `USTRUCT` — never replicated.

```cpp
struct FPendingGroupInvite
{
    // The PlayerState who sent the invite.
    TWeakObjectPtr<APlayerState> InviterPS;

    // The PlayerState being invited.
    TWeakObjectPtr<APlayerState> TargetPS;

    // World time (GetWorld()->GetTimeSeconds()) when this invite expires.
    float ExpiryTime = 0.f;

    // True for raid invites (inviting a group into a raid).
    // In this case TargetPS must be a group leader.
    bool bIsRaidInvite = false;
};
```

---

## FDisconnectGraceEntry

Held server-side in `UGroupSubsystem` for members within the grace window. Not a `USTRUCT` — never replicated.

```cpp
struct FDisconnectGraceEntry
{
    // The group this disconnected member belongs to.
    TWeakObjectPtr<AGroupActor> Group;

    // Slot index in the group to restore on reconnect.
    uint8 SlotIndex = 0;

    // UniqueNetId string to match the reconnecting player.
    FString UniqueNetIdString;

    // World time when the grace window expires.
    float ExpiryTime = 0.f;

    // Timer handle for the grace window cleanup.
    FTimerHandle TimerHandle;
};
```

---

## UGroupConfigDataAsset

**Type:** `UDataAsset` | **Assigned via:** Project Settings → GameCore → `GroupConfig`

```cpp
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
    // Acts as a heartbeat to correct any missed dirty updates.
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
```

---

## GMS Event Message Structs

Defined here — one struct per channel. All use `EGameCoreEventScope::ServerOnly`.

Native tag handles cached in `GameCoreGroupTags` namespace (defined in `GroupTypes.h`, initialized in `FGameCoreModule::StartupModule`):

```cpp
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
```

### FGroupInviteMessage

| Field | Value |
|---|---|
| Channels | `GameCoreEvent.Group.InviteReceived`, `GameCoreEvent.Group.InviteExpired`, `GameCoreEvent.Group.InviteDeclined` |
| Scope | `ServerOnly` |

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGroupInviteMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<APlayerState> InviterPS;
    UPROPERTY() TWeakObjectPtr<APlayerState> TargetPS;
    UPROPERTY() bool bIsRaidInvite = false;
};
```

### FGroupMemberChangedMessage

| Field | Value |
|---|---|
| Channels | `GameCoreEvent.Group.MemberJoined`, `MemberLeft`, `MemberKicked`, `MemberDisconnected`, `MemberReconnected` |
| Scope | `ServerOnly` |

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGroupMemberChangedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<APlayerState> AffectedPS;
    UPROPERTY() TWeakObjectPtr<AGroupActor>  GroupActor;
};
```

### FGroupLeaderChangedMessage

| Field | Value |
|---|---|
| Channels | `GameCoreEvent.Group.LeaderChanged` |
| Scope | `ServerOnly` |

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGroupLeaderChangedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<AGroupActor>  GroupActor;
    UPROPERTY() TWeakObjectPtr<APlayerState> NewLeaderPS;
    UPROPERTY() TWeakObjectPtr<APlayerState> OldLeaderPS; // May be null if old leader disconnected.
};
```

### FGroupDisbandedMessage

| Field | Value |
|---|---|
| Channels | `GameCoreEvent.Group.Disbanded` |
| Scope | `ServerOnly` |

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGroupDisbandedMessage
{
    GENERATED_BODY()
    // GroupActor is about to be destroyed — do not hold beyond the callback frame.
    UPROPERTY() TWeakObjectPtr<AGroupActor> GroupActor;
};
```

### FRaidGroupChangedMessage

| Field | Value |
|---|---|
| Channels | `GameCoreEvent.Raid.GroupJoined`, `GameCoreEvent.Raid.GroupLeft` |
| Scope | `ServerOnly` |

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRaidGroupChangedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<ARaidActor>  RaidActor;
    UPROPERTY() TWeakObjectPtr<AGroupActor> GroupActor;
};
```

### FRaidLeaderChangedMessage

| Field | Value |
|---|---|
| Channels | `GameCoreEvent.Raid.LeaderChanged` |
| Scope | `ServerOnly` |

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRaidLeaderChangedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<ARaidActor>  RaidActor;
    UPROPERTY() TWeakObjectPtr<APlayerState> NewRaidLeaderPS;
    UPROPERTY() TWeakObjectPtr<APlayerState> OldRaidLeaderPS;
};
```

### FRaidDisbandedMessage

| Field | Value |
|---|---|
| Channels | `GameCoreEvent.Raid.Disbanded` |
| Scope | `ServerOnly` |

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRaidDisbandedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<ARaidActor> RaidActor;
};
```
