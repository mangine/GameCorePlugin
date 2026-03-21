# Group System — Architecture

**Part of:** GameCore Plugin | **Status:** Active Specification | **UE Version:** 5.7

The Group System provides invite-based player grouping and multi-group raid formation. It is **server-authoritative** and **fully ephemeral** — no state survives a server restart. Clients receive only a lightweight replicated snapshot of their own group via `FGroupMemberView` on their `APlayerState`. They never hold references to server-side group or raid actors.

---

## Dependencies

### Unreal Engine Modules
| Module | Usage |
|---|---|
| `Engine` | `AActor`, `UActorComponent`, `APlayerState`, `AGameMode` |
| `GameplayTags` | `FGameplayTag`, event channel names |
| `GameplayMessageRuntime` | `UGameplayMessageSubsystem` (via `UGameCoreEventBus`) |
| `NetCore` | `COND_OwnerOnly` replication, `FUniqueNetIdRepl` |

### GameCore Plugin Systems
| System | Usage |
|---|---|
| **Event Bus** (`UGameCoreEventBus`) | All state-change broadcasts (`MemberJoined`, `LeaderChanged`, `Disbanded`, etc.) |
| **GameCore Core** (`IGroupProvider`) | Interface consumed by Quest, Requirement, and Faction systems. Defined in Core — no reverse dependency |

> The Group System has **no hard dependency** on any other GameCore system at the module level. Quest, Requirement, and Faction systems consume group data only through `IGroupProvider`, which is defined in GameCore Core. Wiring lives in the game module.

---

## Requirements

- A group contains 2–N members (default max 5, configurable per project via `UGroupConfigDataAsset`).
- A raid contains 2–M groups (default max 4, configurable).
- `AGroupActor` and `ARaidActor` are server-only actors (`bReplicates = false`). Clients never reference them.
- Each `APlayerState` carries a replicated `FGroupMemberView` — a lightweight snapshot pushed by the server. This is the sole client-facing data source.
- Group formation is invite-based. The leader sends an invite; the target accepts or declines. Invites expire after a configurable timeout (default 30 s).
- A `ForceAddMember` bypass exists for programmatic formation (matchmaking). Enforces max size but skips invite validation. **Server-only.**
- Every group has exactly one **leader** with full authority (invite, kick, promote, pass leadership, disband).
- A raid has one **raid leader** plus zero or more **raid assistants** who can kick members but cannot manage leadership.
- Raid leaders invite whole groups by targeting a group leader.
- Leadership transfers automatically to the next member (slot order) when the current leader disconnects.
- Raid leadership transfers to the next group leader (by group slot order) when the raid leader's group leaves or disbands.
- A **disconnect grace period** (default 60 s) holds a member's slot before removal. Reconnecting within the window restores the slot seamlessly.
- Developers subclass `UMemberSharedData` to define per-member data for within-group (`GroupSharedDataClass`) and cross-group raid visibility (`RaidSharedDataClass`). Either may be null.
- All state changes broadcast via `UGameCoreEventBus`.
- All mutations are server-authoritative. Clients send RPCs; the server validates and pushes updated `FGroupMemberView` to affected players.
- Fully ephemeral. No `IPersistableComponent` usage.

---

## Features

| Feature | Description |
|---|---|
| Invite system | Timer-backed, server-authoritative. Expire notifications fire to both inviter and target |
| Raid formation | Multi-group container with raid leader + assistants authority model |
| Shared data | Subclassable `UMemberSharedData` for group-scope and raid-scope per-member data |
| Disconnect grace | Slot held for configurable duration; reconnecting restores the player seamlessly |
| Matchmaking bypass | `ForceAddMember` for server-side programmatic formation |
| `IGroupProvider` integration | `AGroupActor` implements `IGroupProvider`; Quest/Requirement systems consume it without importing the Group module |
| Fully configurable | `UGroupConfigDataAsset` exposes all limits and timings |

---

## Design Decisions

| Decision | Rationale |
|---|---|
| Server-only group/raid actors, no replication | Clients receive only `FGroupMemberView`. Avoids relevancy: group members may be in a different zone. |
| `FGroupMemberView` per `APlayerState`, `COND_OwnerOnly` | Each player sees only their own view. No single actor pushes state to all members. Scales cleanly to raid size. |
| `UMemberSharedData` as UCLASS, not a C++ template | Templates are incompatible with `UPROPERTY`, `NetSerialize`, and Blueprint. Subclassable `UObject` with virtual methods gives full flexibility. |
| Dual shared data scopes (group vs raid) | Group members need more detail (HP, class icon) than cross-group raid members (name, role). Null `RaidSharedDataClass` avoids sending data to up to 20 players unnecessarily. |
| Invite expiry is server-side timer | Clients cannot be trusted to report expiry. |
| Leader auto-promote on disconnect, slot order | Avoids requiring a vote during urgent gameplay. Slot order is deterministic. |
| `ForceAddMember` is server C++ only, not an RPC | An RPC bypass would be a security hole. Matchmaking runs entirely server-side. |
| Disconnect grace holds the slot | Crash-and-reconnect is an MMORPG reality. Losing a slot on a 1-second disconnect creates support burden. |
| Raid assistants can kick, not promote | Avoids conflicting authority between multiple full leaders. Industry-standard pattern (WoW, FFXIV). |
| `IGroupProvider` decouples consumers | Quest, Requirement, Faction never import the Group module. |
| Fully ephemeral | Group state is session-scoped. Persisting across restarts introduces stale-state edge cases with no gameplay benefit. |
| Disband when group drops to 1 | A solo player has no group. Holding a 1-member group creates orphan actors and complicates query logic. |
| Raid dissolves when only 1 group remains | A single-group raid is a normal group. Auto-dissolve prevents orphan raid actors. |

---

## Logic Flow

### Class Interaction Overview

```
[Server]
  UGroupSubsystem
    ├── spawns/destroys AGroupActor and ARaidActor
    ├── tracks all active groups and raids via PlayerToGroup map
    ├── manages pending invites (expiry via repeating timer)
    └── manages disconnect grace timers (per-player FTimerHandle)

  AGroupActor  (server-only AActor)
    ├── UGroupComponent           ← member list, leader, mutations, shared data tick
    └── implements IGroupProvider ← consumed by Quest, Requirement systems

  ARaidActor  (server-only AActor)
    └── URaidComponent            ← group list, raid leader, raid assistants

[APlayerState (server + owning client)]
  UGroupMemberComponent
    ├── FGroupMemberView   ← COND_OwnerOnly replicated snapshot
    └── Server RPCs        ← Invite, Accept, Decline, Leave, Kick, Transfer, Raid ops

[Client]
  Reads FGroupMemberView on their own APlayerState only.
  Never references AGroupActor or ARaidActor.
  UI binds to OnGroupViewChanged on UGroupMemberComponent.
```

### Invite Flow

```
[Leader Client]
  ServerRPC_InvitePlayer(TargetPS)
    → Server: UGroupSubsystem::CreateInvite
        → validate: caller is leader, group not full, target not already in group
        → create FPendingGroupInvite, register expiry timer
        → UGameCoreEventBus::Broadcast(InviteReceived, target)

[Target Client]
  UI shows invite prompt (driven by event)
  ServerRPC_AcceptInvite()  OR  ServerRPC_DeclineInvite()

[Server on Accept]
  UGroupSubsystem::AcceptInvite
    → validate invite still valid (not expired, inviter still in group)
    → if inviter has no group: CreateGroup(InviterPS)
    → UGroupComponent::AddMember(TargetPS)
    → PushViewToAllMembers
    → Broadcast(MemberJoined)

[Server on Decline or Expiry]
  Remove FPendingGroupInvite
  Broadcast(InviteDeclined or InviteExpired) to inviter
```

### Disconnect Grace Flow

```
[Member disconnects]
  AGameMode::NotifyPlayerDisconnected
    → UGroupSubsystem::OnPlayerDisconnected(PS)
        → if PS is leader: TransferLeadershipToNextSlot immediately
        → MarkMemberDisconnected on UGroupComponent
        → PushViewToAllMembers
        → register FDisconnectGraceEntry + FTimerHandle
        → Broadcast(MemberDisconnected)

[Member reconnects within grace window]
  AGameMode::PostLogin (same UniqueNetId)
    → UGroupSubsystem::OnPlayerReconnected(PS)
        → find grace entry by UniqueNetId
        → ClearTimer
        → UGroupComponent::RestoreMember(PS, SlotIndex)
        → PushViewToAllMembers
        → Broadcast(MemberReconnected)

[Grace window expires]
  FTimerDelegate fires → OnGraceExpired(UniqueNetIdString)
    → UGroupComponent::RemoveMember by UniqueNetId
    → standard member-left flow (leader transfer already done at disconnect time)
    → Broadcast(MemberLeft)
```

### Shared Data Update Flow

```
[Server tick, per UGroupComponent]
  TimeSinceLastHeartbeat += DeltaTime
  if any entry is dirty OR heartbeat interval elapsed:
    GatherAllSharedData (calls GatherFromPlayerState per entry)
    PushViewToAllMembers
      → GroupSharedData pushed to own-group members only
      → RaidSharedData  pushed to cross-group raid members only (via BuildViewForMember)
```

### View Push Flow (PushViewToAllMembers)

```
UGroupComponent::PushViewToAllMembers
  For each non-disconnected FGroupMemberEntry:
    find UGroupMemberComponent on MemberPS
    call SetGroupView(BuildViewForMember(MemberPS))
      → FGroupMemberView.Members  = own group entries (with GroupSharedData)
      → FGroupMemberView.RaidMembers = cross-group entries from URaidComponent
                                       (GroupSharedData=null, RaidSharedData only)
    COND_OwnerOnly replication delivers view to that client only
    OnRep_GroupView fires on client → OnGroupViewChanged delegate
```

---

## GMS Event Tags

Defined in `DefaultGameplayTags.ini` inside the GameCore module. Native handles cached in `GameCoreGroupTags` namespace at startup.

```ini
[/Script/GameplayTags.GameplayTagsList]
+GameplayTagList=(Tag="GameCoreEvent.Group.InviteReceived")
+GameplayTagList=(Tag="GameCoreEvent.Group.InviteExpired")
+GameplayTagList=(Tag="GameCoreEvent.Group.InviteDeclined")
+GameplayTagList=(Tag="GameCoreEvent.Group.Formed")
+GameplayTagList=(Tag="GameCoreEvent.Group.MemberJoined")
+GameplayTagList=(Tag="GameCoreEvent.Group.MemberLeft")
+GameplayTagList=(Tag="GameCoreEvent.Group.MemberKicked")
+GameplayTagList=(Tag="GameCoreEvent.Group.MemberDisconnected")
+GameplayTagList=(Tag="GameCoreEvent.Group.MemberReconnected")
+GameplayTagList=(Tag="GameCoreEvent.Group.LeaderChanged")
+GameplayTagList=(Tag="GameCoreEvent.Group.Disbanded")
+GameplayTagList=(Tag="GameCoreEvent.Raid.GroupJoined")
+GameplayTagList=(Tag="GameCoreEvent.Raid.GroupLeft")
+GameplayTagList=(Tag="GameCoreEvent.Raid.LeaderChanged")
+GameplayTagList=(Tag="GameCoreEvent.Raid.Disbanded")
```

All broadcasts use `EGameCoreEventScope::ServerOnly` — the server owns all group state. Clients react to `FGroupMemberView` replication, not to events.

---

## Known Issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| 1 | `AssignRaidRoles` has a broken loop body — iterates `TArray<APlayerState*>{}` (empty literal) instead of calling `GetAllMembers` | **High** | Must call `GC->GetAllMembers(Members)` before iterating. See Code Review. |
| 2 | `PopulateRaidMemberEntries` references `Entry.RaidRole` without a concrete way to read it — `UGroupComponent` has no `GetRaidRole(APlayerState*)` accessor | **Medium** | Add `GetRaidRole(const APlayerState*)` to `UGroupComponent`, or store raid role in `FGroupMemberEntry` and expose a reader. |
| 3 | `RemoveGroup` checks `IsMember` on a group whose membership is still intact at call time — the `RemoveAll` lambda for assistants may be unreliable if the group has already removed the player before `RemoveGroup` is called | **Low** | Clarify call order: `RemoveMember` then `RemoveGroup`, or `RemoveGroup` then group self-cleans. |
| 4 | `IsGroupLeader(const APlayerState* PS)` in `IGroupProvider` is called with `PS == this` in the game-module sample — the interface contract is ambiguous: does it mean "is PS the leader of the group *this* provider represents" or "is the owner of this provider a leader"? | **Low** | Document clearly in `IGroupProvider` interface. |
| 5 | No `Kicked` notify is sent to the kicked player client in `KickMember` at the group component level — only a comment exists | **Low** | Implement the GMS broadcast to the kicked player's connection. |
| 6 | `UGroupSubsystem::CreateInvite` does not validate whether the target is already in a group (only noted in the invite flow description, not visible in the subsystem spec) | **Medium** | Add explicit `FindGroupForPlayer(TargetPS) == nullptr` check in `CreateInvite`. |
| 7 | Shared data objects are `NewObject`'d with `GetOwner()` (the `AGroupActor`) as outer — when the group is disbanded and the actor destroyed, these UObjects become garbage-collected. This is correct but must be documented so subclassers don't store raw pointers externally. | **Low** | Document lifetime in `UMemberSharedData`. |

---

## File Structure

```
GameCore/Source/GameCore/
└── Group/
    ├── GroupTypes.h                  ← enums, structs, UMemberSharedData, UGroupConfigDataAsset, event message structs
    ├── GroupSubsystem.h / .cpp       ← UGroupSubsystem
    ├── GroupActor.h / .cpp           ← AGroupActor
    ├── GroupComponent.h / .cpp       ← UGroupComponent
    ├── RaidActor.h / .cpp            ← ARaidActor
    ├── RaidComponent.h / .cpp        ← URaidComponent
    └── GroupMemberComponent.h / .cpp ← UGroupMemberComponent (on APlayerState)
```

No editor-only module files — the Group System has no editor tooling requirements.
