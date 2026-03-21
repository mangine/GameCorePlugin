# Group System — Code Review

---

## Overview

The Group System is architecturally sound at its highest level: server-authoritative ephemeral state, per-player replicated snapshot (`COND_OwnerOnly`), invite-based formation with server expiry, and `IGroupProvider` for decoupled consumption. The design is consistent with industry-standard party systems (WoW, FFXIV) and correctly avoids persistence for session-scoped data.

However, several specific implementation issues were found that require fixes before the system is implementation-ready.

---

## Critical Issues

### 1. `AssignRaidRoles` iterates an empty literal instead of group members

**Original code (broken):**
```cpp
for (APlayerState* PS : TArray<APlayerState*>{})
```
This is an empty array literal. The loop body never executes. All raid roles will remain at their default value (`None`) and will never be updated.

**Fix:** Call `GetAllMembers` before iterating:
```cpp
TArray<APlayerState*> Members;
GC->GetAllMembers(Members);
for (APlayerState* PS : Members) { ... }
```

Fixed in the new `URaidComponent.md` spec.

---

### 2. `PopulateRaidMemberEntries` reads `Entry.RaidRole` from a local empty struct

**Original code:**
```cpp
Entry.RaidRole = /* read from stored role */;
```
This is a stub comment, not an implementation. Without `UGroupComponent::GetRaidRole(APlayerState*)` and `UGroupComponent::SetRaidRoleForMember`, cross-group entries will have `RaidRole = None` regardless of actual role.

**Fix:** Added `SetRaidRoleForMember` and `GetRaidRole` to `UGroupComponent` in the new spec. `PopulateRaidMemberEntries` now calls `Group->GetGroupComponent()->GetRaidRole(PS)` directly.

---

## Significant Issues

### 3. `UGroupSubsystem::CreateInvite` does not validate target group membership

The original flow description says to check "target not already grouped" but the subsystem code doesn't include this check explicitly. A malicious RPC could cause a player to receive an invite while already in a group, and accepting it could result in them being in two groups simultaneously if `FindGroupForPlayer` is not checked in `AddMember`.

**Fix:** Added explicit `FindGroupForPlayer(TargetPS) != nullptr` guard in `CreateInvite` in the new spec.

---

### 4. Raid kick goes through `KickMember(groupLeader, target)` — wrong authority model

`URaidComponent::KickMember` calls `UGroupComponent::KickMember(groupLeader, target)`. This has two problems:
- If the kicked player is the group leader, `GetLeader() == kickerPS` check passes (using the group's own leader as proxy) but the semantic is wrong — the raid kicked this player, not the group leader.
- If the group leader is disconnected (grace state), `GetLeader()` returns a different player, and the kick silently fails.

**Fix recommended:** Add `UGroupComponent::ForceRemoveMember(APlayerState*)` — a server-authority-bypass removal that does not check group leader. Raid component calls this for cross-group kicks. Noted in `URaidComponent.md` as an implementation note.

---

### 5. `DisbandGroup` called from inside `RemoveMember` — re-entrant call risk

`RemoveMember` calls `UGroupSubsystem::DisbandGroup`, which calls `Group->Destroy()`. If `DisbandGroup` iterates `ActiveGroups` while `RemoveMember` is still on the call stack via a `UGroupComponent` method, the actor is destroyed mid-execution. UE's actor destruction defers the actual memory invalidation to the next frame, so this is usually safe, but accessing `Members` after the `DisbandGroup` call is undefined.

**Fix applied:** Added `return;` immediately after the `DisbandGroup` call in `RemoveMember` and `RemoveMemberByNetId` to prevent any further access to `Members` after disband is triggered.

---

### 6. `RemoveGroup` assistant cleanup may fail if member removal precedes group removal

`URaidComponent::RemoveGroup` calls `Group->GetGroupComponent()->IsMember(PS)` inside a `RemoveAll` lambda to identify assistants that belonged to the leaving group. However, if `RemoveMember` has already been called on the group's members (e.g., during a group disband sequence), `IsMember` will return false for all of them and the assistants won't be cleaned up.

**Recommendation:** Capture the assistant set for the leaving group *before* any member removal. Alternatively, track the group pointer in the assistant list (instead of just `TWeakObjectPtr<APlayerState>`) so cleanup can be done by group identity.

---

## Minor Issues

### 7. `UGroupMemberComponent` caches `UGroupSubsystem` as a `TWeakObjectPtr` member

The original spec stores a `TWeakObjectPtr<UGroupSubsystem>` as a member field named `CachedSubsystem`. `UWorldSubsystem` instances are managed by the engine and are never garbage-collected while the world is alive. Caching introduces complexity for no benefit.

**Fix applied:** `GetSubsystem()` calls `GetWorld()->GetSubsystem<UGroupSubsystem>()` directly, no caching. The call overhead is negligible (hash map lookup, ~10ns).

---

### 8. No `Kicked` notification reaches the kicked player

In the original `UGroupComponent::KickMember`, there is only a comment noting a GMS event should fire. The kicked player's client would never know they were kicked — their `GroupView` would simply be cleared next replication tick with no context.

**Fix applied:** `KickMember` now explicitly broadcasts `GameCoreGroupTags::MemberKicked` after removal. The kicked player's UI can listen for this event (filtered by `AffectedPS`) to show a kick notification before the view clears.

---

### 9. `IGroupProvider::IsGroupLeader` signature is ambiguous in the game-module sample

The original wiring example implements:
```cpp
bool AMyPlayerState::IsGroupLeader(const APlayerState* PS) const
    { return PS == this && ...; }
```
The `PS == this` guard assumes the caller always passes `this` as `PS`, but the interface signature `IsGroupLeader(const APlayerState* PS)` suggests it should mean "is *PS* the leader of the group this provider represents". This is a contract ambiguity — if a quest system asks `provider->IsGroupLeader(someOtherPS)`, the check will always return false regardless of that player's actual leadership.

**Recommendation:** Clarify `IGroupProvider::IsGroupLeader` in the Core spec to mean "is the given PS the leader of the group this provider is bound to". The `PS == this` short-circuit in the game-module implementation is wrong unless the provider is always called in a self-referential context.

---

### 10. `FGroupMemberView` replicates `TObjectPtr<UMemberSharedData>` — `UObject`s in structs are fragile

Replicating `UObject*` pointers inside struct arrays (`TArray<FGroupMemberEntry>`) with `COND_OwnerOnly` relies on the object being network-addressable (registered in the `UPackageMap`). Since `UMemberSharedData` instances are `NewObject`'d locally on the server with no replication of their own, they will not be in the client's package map.

**Recommendation:** The shared data should be serialized as raw data (via `NetSerialize`) rather than as `UObject*` references in a replicated struct. Either:
- Remove `TObjectPtr<UMemberSharedData>` from `FGroupMemberEntry` and instead serialize the data inline using `FGroupMemberEntry::NetSerialize`, or
- Replicate a flat data payload (`TArray<uint8>`) alongside the struct and reconstruct the object client-side.

This is the most significant architectural gap in the current design and will cause replication failures in shipping code.

---

## Design Observations

### Shared data tick on every group component is expensive at scale

Each `AGroupActor` ticks every frame (when shared data is configured). With 1000 concurrent groups, this is 1000 `TickComponent` calls checking dirty flags. Consider:
- Moving heartbeat management to `UGroupSubsystem` with a single coalesced timer, or
- Using a dirty-only push model (no heartbeat, `GatherFromPlayerState` fires on stat change events from the owning system) to eliminate the tick entirely on quiescent groups.

### `PlayerToGroup` map in `UGroupSubsystem` is good — don't remove it

The `TMap<FString, TWeakObjectPtr<AGroupActor>> PlayerToGroup` fast lookup is correct and efficient. The alternative — iterating `ActiveGroups` and calling `IsMember` on each — would be O(N×M) where N = groups and M = group size. Keep this map and ensure it is always updated in sync with member adds/removes.

### No rate-limiting on invite RPCs

`ServerRPC_InvitePlayer` has no cooldown check. A malicious client could spam invites to create thousands of `FPendingGroupInvite` entries in the subsystem map. 

**Recommendation:** Add a per-player invite cooldown (e.g., 1-second minimum between invites) enforced server-side in `CreateInvite`.

### `ForceAddMember` vs `AddMember` share all logic

`ForceAddMember` simply calls `AddMember`. The only distinction is the `check(!IsRunningClientOnly())` guard at the top. This is correct — the bypass is at the *call site* (no invite validation in the subsystem), not in the component logic. This is documented clearly and is a good pattern.

### Group disband threshold of ≤1 member is correct

Disbanding when the group drops to 1 member (solo) is the right call. Keeping a 1-member group alive creates orphan actors, complicates `IGroupProvider` queries, and provides no gameplay value. Auto-disband on solo is the correct industry pattern.
