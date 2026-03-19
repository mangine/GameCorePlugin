# Group System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Group System provides invite-based player grouping and multi-group raid formation. It is server-authoritative, fully ephemeral (no persistence across server restarts), and designed to be consumed by other GameCore systems (Quest, Requirement, Faction, etc.) through the existing `IGroupProvider` interface without coupling to this module directly.

---

# Requirements

The following are the hard requirements this system was designed to satisfy:

- A group contains 2 to N members (default max 5, configurable per project).
- A raid contains 2 to M groups (default max 4, configurable per project).
- Groups and raids are represented by server-only actors (`AGroupActor`, `ARaidActor`). Clients never reference these actors directly.
- Each `APlayerState` carries a replicated `FGroupMemberView` — a lightweight snapshot of its group/raid state, pushed by the server. This is the sole client-facing data source.
- Group formation is invite-based. The group leader sends an invite; the target accepts or declines. Invites expire after a configurable timeout (default 30s).
- A server-side `ForceAddMember` bypass exists for programmatic group formation (matchmaking). It enforces max size but skips invite validation.
- Every group has exactly one **leader** with full authority (invite, kick, promote, pass leadership, disband).
- A raid has exactly one **raid leader** with full authority, plus zero or more **raid assistants** who can kick members but cannot promote/demote assistants or transfer raid leadership.
- Raid leaders can invite whole groups (by targeting a group leader). Raid invites are declined at the group level by the group leader.
- The group leader can kick members within their own group. The raid leader can kick any member across all groups.
- Leadership transfers automatically to the next member (by slot order) when the current leader disconnects.
- Raid leadership transfers to the next group leader (by group slot order) when the raid leader's group leaves or disbands.
- A **disconnect grace period** (default 60s, configurable) holds a member's slot before they are removed from the group. Reconnecting within the window restores the member seamlessly.
- Developers may define two subclasses of `UMemberSharedData` per project: one for within-group visibility (`GroupSharedDataClass`) and one for cross-group raid visibility (`RaidSharedDataClass`). Either may be null to share nothing at that scope.
- The system broadcasts GMS events for all major state changes (member join/leave, leader change, disband, etc.).
- All group mutations are server-authoritative. Clients send RPCs; the server validates and pushes updated `FGroupMemberView` to affected players.
- This system is fully ephemeral. No state is saved or restored across server restarts.
- `AGroupActor` implements `IGroupProvider` so that other GameCore systems (Quest, Requirement) can consume group data without importing this module.

---

# Key Design Decisions

| Decision | Rationale |
|---|---|
| Server-only group/raid actors, no replication | Clients receive only the lightweight `FGroupMemberView` on their `APlayerState`. Avoids relevancy problems: a player's group members may be in a different zone. |
| `FGroupMemberView` per `APlayerState`, owner-only replication | Each player sees only their own view. No single replicated actor needs to push state to all members. Scales cleanly to raid sizes. |
| `UMemberSharedData` as a UCLASS base, not a C++ template | Templates are incompatible with `UPROPERTY`, `NetSerialize`, and Blueprint. A subclassable `UObject` with virtual `GatherFromPlayerState` and `NetSerialize` gives full flexibility without those constraints. |
| Dual shared data scopes (group vs raid) | Group members typically need more detail (HP bars, class icons) than raid members of other groups (name, role only). Letting `RaidSharedDataClass` be null avoids sending per-member data to up to 20 players unnecessarily. |
| Invite expiry is server-side timer, not client | Clients cannot be trusted to report expiry. The server holds `FPendingGroupInvite` with a `float ExpiryTime` and cleans up on tick. |
| Leader auto-promote on disconnect, slot order | Avoids requiring a vote during a crisis (boss fight, urgent content). Slot order is deterministic. |
| `ForceAddMember` is server C++ only, not an RPC | Matchmaking and instanced content systems run entirely server-side. An RPC bypass would expose a security hole. |
| Disconnect grace period holds the slot | Crash-and-reconnect is a constant MMORPG reality. Losing a slot on a 1s disconnect is a UX failure that creates support burden. |
| Raid assistants can kick, not promote | Avoids conflicting authority between multiple full raid leaders. One primary raid leader with delegated kick authority is the industry-standard pattern (WoW, FFXIV). |
| `IGroupProvider` decouples consumers | Quest, Requirement, and Faction systems never import the Group module. They read group state through `IGroupProvider` on `APlayerState`, which the game module wires to this system. |
| Fully ephemeral — no persistence | Group state is session-scoped by design. Persisting party composition across server restarts introduces stale-state edge cases (members offline, full group on reconnect) with no gameplay benefit. |

---

# System Units

| Unit | Class | Lives On |
|---|---|---|
| Shared data base class | `UMemberSharedData` | Abstract `UObject` — subclassed per game |
| Config | `UGroupConfigDataAsset` | `UDataAsset` — one per project |
| Member entry & view structs | `FGroupMemberEntry`, `FGroupMemberView` | `GroupTypes.h` |
| Pending invite struct | `FPendingGroupInvite` | `GroupTypes.h` |
| Group actor | `AGroupActor` | Server-only `AActor` |
| Group component | `UGroupComponent` | `UActorComponent` on `AGroupActor` |
| Raid actor | `ARaidActor` | Server-only `AActor` |
| Raid component | `URaidComponent` | `UActorComponent` on `ARaidActor` |
| Player-facing component | `UGroupMemberComponent` | `UActorComponent` on `APlayerState` |
| Group subsystem | `UGroupSubsystem` | `UWorldSubsystem` |

---

# How the Pieces Connect

```
[Server]
  UGroupSubsystem
    → spawns/destroys AGroupActor and ARaidActor
    → tracks all active groups and raids
    → manages pending invites and disconnect grace timers

  AGroupActor
    ├── UGroupComponent         ← member list, leader, mutations
    └── USharedQuestCoordinator ← (from Quest System, optional)
    implements IGroupProvider   ← consumed by Quest, Requirement systems

  ARaidActor
    └── URaidComponent          ← group list, raid leader, raid assistants

[APlayerState (server + owning client)]
  UGroupMemberComponent
    ├── FGroupMemberView         ← replicated to owning client only
    └── ServerRPC entry points   ← Invite, Accept, Decline, Leave, etc.

[Client]
  Reads FGroupMemberView on their own APlayerState
  Never references AGroupActor or ARaidActor
  UI polls / binds to OnGroupViewChanged delegate on UGroupMemberComponent
```

---

# Invite Flow

```
[Leader Client]
  ServerRPC_InvitePlayer(TargetPS)
    → Server: validate leader status, group not full, target not already grouped
    → Server: create FPendingGroupInvite, start expiry timer
    → Server: fire GameCoreEvent.Group.InviteReceived to target's connection via GMS

[Target Client]
  UI shows invite prompt (driven by GMS event)
  ServerRPC_AcceptInvite()  OR  ServerRPC_DeclineInvite()

[Server on Accept]
  → validate invite still valid (not expired, inviter still in group)
  → UGroupSubsystem: if inviter has no group, spawn AGroupActor first
  → UGroupComponent::AddMember(TargetPS)
  → push updated FGroupMemberView to all current members
  → fire GameCoreEvent.Group.MemberJoined

[Server on Decline or Expiry]
  → remove FPendingGroupInvite
  → fire GameCoreEvent.Group.InviteDeclined or GameCoreEvent.Group.InviteExpired to inviter
```

---

# Disconnect Grace Flow

```
[Member disconnects]
  AGameMode::NotifyPlayerDisconnected
    → UGroupSubsystem::OnPlayerDisconnected(PS)
        → find group containing PS
        → mark FGroupMemberEntry::bDisconnected = true
        → start FDisconnectGraceTimer (GraceSeconds from config)
        → if PS was leader: transfer leadership immediately (next slot order)
        → push updated FGroupMemberView to remaining members
        → fire GameCoreEvent.Group.MemberDisconnected

[Member reconnects within grace window]
  AGameMode::PostLogin (same UniqueNetId)
    → UGroupSubsystem::OnPlayerReconnected(PS)
        → find grace timer entry by UniqueNetId
        → cancel timer
        → replace TWeakObjectPtr<APlayerState> in FGroupMemberEntry with new PS
        → restore UGroupMemberComponent state on new PS
        → push updated FGroupMemberView to all members
        → fire GameCoreEvent.Group.MemberReconnected

[Grace window expires without reconnect]
  FDisconnectGraceTimer fires
    → UGroupComponent::RemoveMember(UniqueNetId)
    → standard member-left flow (leader transfer already done)
    → fire GameCoreEvent.Group.MemberLeft
```

---

# Shared Data Update Flow

```
[Server tick, per UGroupComponent]
  For each FGroupMemberEntry:
    if SharedData && (SharedData->IsDirty() || heartbeat interval elapsed):
      SharedData->GatherFromPlayerState(MemberPS)
      SharedData->ClearDirty()
      push updated FGroupMemberView to all members in scope:
        - GroupSharedData → pushed to own-group members only
        - RaidSharedData  → pushed to other-group raid members only
```

---

# GMS Event Tags

```ini
; DefaultGameplayTags.ini
+GameplayTagList=(Tag="GameCoreEvent.Group.InviteReceived")
+GameplayTagList=(Tag="GameCoreEvent.Group.InviteExpired")
+GameplayTagList=(Tag="GameCoreEvent.Group.InviteDeclined")
+GameplayTagList=(Tag="GameCoreEvent.Group.Formed")
+GameplayTagList=(Tag="GameCoreEvent.Group.MemberJoined")
+GameplayTagList=(Tag="GameCoreEvent.Group.MemberLeft")
+GameplayTagList=(Tag="GameCoreEvent.Group.MemberDisconnected")
+GameplayTagList=(Tag="GameCoreEvent.Group.MemberReconnected")
+GameplayTagList=(Tag="GameCoreEvent.Group.LeaderChanged")
+GameplayTagList=(Tag="GameCoreEvent.Group.Disbanded")
+GameplayTagList=(Tag="GameCoreEvent.Raid.GroupJoined")
+GameplayTagList=(Tag="GameCoreEvent.Raid.GroupLeft")
+GameplayTagList=(Tag="GameCoreEvent.Raid.LeaderChanged")
+GameplayTagList=(Tag="GameCoreEvent.Raid.Disbanded")
```

---

# Quick Integration Guide

## Minimum Setup

1. Create a `UGroupConfigDataAsset` asset. Set `MaxGroupSize`, `MaxGroupsPerRaid`, `InviteExpirySeconds`, `DisconnectGraceSeconds`, and optionally `GroupSharedDataClass` / `RaidSharedDataClass`.
2. Open **Project Settings → GameCore** and assign the asset to `GroupConfig`.
3. Add `UGroupMemberComponent` to your `APlayerState` subclass.
4. In `AGameMode`, call `UGroupSubsystem::OnPlayerDisconnected` from `NotifyPlayerDisconnected`, and `UGroupSubsystem::OnPlayerReconnected` from `PostLogin`.
5. Implement `IGroupProvider` on `APlayerState` by forwarding to `UGroupMemberComponent`. This satisfies the Quest System and any other `IGroupProvider` consumers.

## Defining Shared Data

```cpp
// Game module — PirateGroupMemberData.h
UCLASS()
class UPirateGroupMemberData : public UMemberSharedData
{
    GENERATED_BODY()
public:
    UPROPERTY()
    FText CharacterName;

    UPROPERTY()
    int32 CurrentHP = 0;

    UPROPERTY()
    int32 MaxHP = 0;

    virtual void GatherFromPlayerState(APlayerState* PS) override
    {
        if (APiratePlayerState* PPS = Cast<APiratePlayerState>(PS))
        {
            CharacterName = PPS->GetCharacterName();
            CurrentHP     = PPS->GetCurrentHP();
            MaxHP         = PPS->GetMaxHP();
            MarkDirty();
        }
    }

    virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override
    {
        Ar << CharacterName;
        Ar << CurrentHP;
        Ar << MaxHP;
        bOutSuccess = true;
        return true;
    }
};
```

Assign `UPirateGroupMemberData::StaticClass()` to `UGroupConfigDataAsset::GroupSharedDataClass`.

## Sending an Invite (C++ game code)

```cpp
// Triggered from player input, validated client-side for UI feedback only.
// Real validation happens server-side inside the RPC.
UGroupMemberComponent* GroupComp = GetPlayerState()->FindComponentByClass<UGroupMemberComponent>();
GroupComp->ServerRPC_InvitePlayer(TargetPlayerState);
```

## Programmatic Group Formation (Matchmaking)

```cpp
// Server-only — call from matchmaking subsystem after players are assigned.
UGroupSubsystem* GS = GetWorld()->GetSubsystem<UGroupSubsystem>();
AGroupActor* Group = GS->CreateGroup(LeaderPlayerState);
for (APlayerState* PS : OtherMembers)
{
    Group->GetGroupComponent()->ForceAddMember(PS);
}
```

## Reading Group State on the Client (UI)

```cpp
// Bind to the delegate on UGroupMemberComponent for reactive UI.
UGroupMemberComponent* Comp = LocalPlayerState->FindComponentByClass<UGroupMemberComponent>();
Comp->OnGroupViewChanged.AddUObject(this, &UGroupHUDWidget::OnGroupViewChanged);

// Or read the current snapshot directly:
const FGroupMemberView& View = Comp->GetGroupView();
for (const FGroupMemberEntry& Entry : View.Members)
{
    // populate HP bar, name plate, etc.
    if (UPirateGroupMemberData* Data = Cast<UPirateGroupMemberData>(Entry.GroupSharedData))
    {
        SetMemberHP(Entry.SlotIndex, Data->CurrentHP, Data->MaxHP);
    }
}
```

---

# Sub-Pages

[GroupTypes — Enums, Structs, UMemberSharedData, UGroupConfigDataAsset](Group%20System/GroupTypes.md)

[UGroupSubsystem](Group%20System/UGroupSubsystem.md)

[AGroupActor and UGroupComponent](Group%20System/AGroupActor%20and%20UGroupComponent.md)

[ARaidActor and URaidComponent](Group%20System/ARaidActor%20and%20URaidComponent.md)

[UGroupMemberComponent](Group%20System/UGroupMemberComponent.md)

---

# File and Folder Structure

```
GameCore/Source/GameCore/
└── Group/
    ├── GroupTypes.h                         ← enums, structs, UMemberSharedData, UGroupConfigDataAsset
    ├── GroupSubsystem.h / .cpp              ← UGroupSubsystem
    ├── GroupActor.h / .cpp                  ← AGroupActor
    ├── GroupComponent.h / .cpp              ← UGroupComponent
    ├── RaidActor.h / .cpp                   ← ARaidActor
    ├── RaidComponent.h / .cpp               ← URaidComponent
    └── GroupMemberComponent.h / .cpp        ← UGroupMemberComponent (on APlayerState)
```

---

# Implementation Constraints

- `AGroupActor` and `ARaidActor` have `bReplicates = false`. They are server-only plumbing. Never read them from a client code path.
- `FGroupMemberView` replicates with `COND_OwnerOnly` on `UGroupMemberComponent`. No member sees another member's raw view.
- All RPCs that mutate group state are `Server` RPCs on `UGroupMemberComponent`. The component lives on `APlayerState`, which has an owning connection, making the RPC path valid.
- `ForceAddMember` must be called server-side only. Add a `check(!IsRunningClientOnly())` guard at the top.
- `UMemberSharedData::GatherFromPlayerState` is called on the server only. Never call it from a client code path.
- The Group System has no dependency on any other GameCore system at the module level. Quest, Requirement, and Faction systems consume it only through `IGroupProvider`, which is defined in GameCore Core.
- `UGroupSubsystem` must be notified of disconnects and reconnects via explicit calls from `AGameMode`. It does not hook `AGameMode` directly to avoid a hard module dependency.
- Maximum group and raid sizes are enforced in both `AddMember`/`ForceAddMember` and the RPC validator. Designers cannot exceed them by crafting malicious RPCs.
