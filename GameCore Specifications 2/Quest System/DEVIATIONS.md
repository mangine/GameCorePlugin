# Quest System — Implementation Deviations

## DEV-1: `FEventWatchHandle` has no `Unregister()` method

**Spec says:** `H->Unregister()` in `Internal_CompleteQuest` and `Internal_FailQuest`.

**What we implemented:**
```cpp
if (UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(this))
    Watcher->Unregister(*H);
```

**Reason:** `FEventWatchHandle` (in `GameCoreEventWatcher.h`) only has an `Id` field and `IsValid()`. The unregister API is on `UGameCoreEventWatcher`. The spec incorrectly treats the handle as a self-unregistering object.

---

## DEV-2: `UGameCoreEventBus::Get()` returns pointer, not reference

**Spec shows:** `UGameCoreEventBus::Get(this).Broadcast(...)`

**What we implemented:** `UGameCoreEventBus::Get(this)->Broadcast(...)` with null check.

**Reason:** `UGameCoreEventBus::Get()` returns `UGameCoreEventBus*` (nullable pointer). We guard all calls with `if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))`.

---

## DEV-3: `IGroupProvider::GetGroupMembers` signature assumed

**Spec reference:** `USharedQuestComponent::ServerRPC_AcceptQuest_Implementation` calls
`Provider->GetGroupMembers(InvitedMembers)` to collect members for LeaderAccept flow.

**What we implemented:** Called as `Provider->GetGroupMembers(InvitedMembers)` where `InvitedMembers` is `TArray<APlayerState*>&`.

**Reason:** The Group System spec defines `IGroupProvider` but the exact `GetGroupMembers` signature is not specified in the Quest System spec. Assumed `void GetGroupMembers(TArray<APlayerState*>& OutMembers) const` as the most natural API. Adjust to match actual Group System implementation.

---

## DEV-4: `FQuestRuntime::PostReplicatedAdd` does not auto-register completion watchers

**Spec says:** `PostReplicatedAdd` should trigger `RegisterClientValidatedCompletionWatchers` for the newly added quest (fixes KI-3).

**What we implemented:** `PostReplicatedAdd` is a no-op in the struct. The struct has no access to the owning `UQuestComponent`.

**Reason:** `FFastArraySerializerItem` callbacks receive `const FQuestRuntimeArray&` but not the owning component. `FQuestRuntimeArray` does not hold a back-pointer to `UQuestComponent`. A proper fix would require storing a `TWeakObjectPtr<UQuestComponent>` in `FQuestRuntimeArray` and calling `RegisterClientValidatedCompletionWatcher` from `PostReplicatedAdd`. Deferred as a known issue (KI-3 from Architecture.md).

---

## DEV-5: Spec's `Serialize_Save` uses `Ar <<` on const members directly

**Spec shows:** `Ar << Q.LastCompletedTimestamp` where `LastCompletedTimestamp` is `int64` on a `const FQuestRuntime&`.

**What we implemented:** Used `const_cast<int64&>(Q.LastCompletedTimestamp)` for the serialize path since `FArchive` overloads require non-const references for `<<`.

**Reason:** UE5's `FArchive::operator<<` is not templated for const references. Standard pattern is to copy to a local or const_cast when reading for serialization on a saving archive.

---

## DEV-6: `SharedQuestComponent` assumes `IGroupProvider::GetGroupActor()` exists

**Spec shows:** `Provider->GetGroupActor()` in `GetCoordinator()`.

**What we implemented:** Same — `Cast<IGroupProvider>(GetOwner())->GetGroupActor()`.

**Reason:** Spec directly references this method. Assumed it is defined on `IGroupProvider` in the Group System. If the Group System uses a different method name, adjust `GetCoordinator()` accordingly.

---

## DEV-7: `UQuestRegistrySubsystem` cadence check uses integer arithmetic for UTC floor

**Spec says:** `ComputeLastDailyReset` = "floor to midnight UTC", `ComputeLastWeeklyReset` = "floor to Monday midnight UTC".

**What we implemented:**
```cpp
static int64 ComputeLastDailyReset(int64 NowTs)  { return (NowTs / 86400LL) * 86400LL; }
static int64 ComputeLastWeeklyReset(int64 NowTs)
{
    // Epoch (1970-01-01) was Thursday. Offset by 3 → Monday-aligned.
    const int64 DaysSinceEpoch   = NowTs / 86400LL;
    const int64 DaysSinceMonday  = (DaysSinceEpoch + 3LL) % 7LL;
    return (DaysSinceEpoch - DaysSinceMonday) * 86400LL;
}
```

**Reason:** Spec does not provide implementation. Used simple UTC integer arithmetic. This correctly floors to midnight and to the most recent Monday. DST does not apply since UTC is used throughout.
