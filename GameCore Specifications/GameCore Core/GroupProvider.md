# IGroupProvider + UGroupProviderDelegates

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

**File:** `GameCore/Source/GameCore/Interfaces/GroupProvider.h`

A generic interface for any actor or component that owns group membership data. Provides a stable contract for reading group state without coupling to any concrete group or party system implementation. Any grouping abstraction — parties, ship crews, squads, guilds — satisfies this interface.

Currently consumed by the Quest System (`USharedQuestComponent`) to drive shared quest enrollment and tracker scaling. Designed for use by any future system that needs to read group state.

---

## `IGroupProvider`

```cpp
UINTERFACE(MinimalAPI, BlueprintType)
class GAMECORE_API UGroupProvider : public UInterface { GENERATED_BODY() };

class GAMECORE_API IGroupProvider
{
    GENERATED_BODY()
public:
    // Number of members currently in the group, including this player.
    // Returns 1 if the player is ungrouped.
    virtual int32 GetGroupSize() const = 0;

    // True if this PlayerState is currently the group leader.
    virtual bool IsGroupLeader() const = 0;

    // All PlayerState members of the group, including this player.
    // OutMembers is valid only for the current frame — do not cache the array.
    virtual void GetGroupMembers(TArray<APlayerState*>& OutMembers) const = 0;

    // Returns the actor that owns the USharedQuestCoordinator for this group.
    // Used by USharedQuestComponent to locate the coordinator without
    // coupling to any concrete group actor type.
    // Returns nullptr if no coordinator actor exists (e.g. player is not in a group).
    virtual AActor* GetGroupActor() const = 0;
};
```

**Design rules:**
- `IGroupProvider` is **read-only** from all consuming systems. No method mutates group state.
- Implemented on `APlayerState` (or delegated from it). This is the canonical owner since `USharedQuestComponent` lives on `APlayerState` and accesses the interface via `Cast<IGroupProvider>(GetOwner())`.
- All methods must be callable on both server and client. Server uses them for authoritative decisions; client uses them for UI feedback.
- `GetGroupMembers` uses an output parameter to avoid per-call heap allocation.

---

## `UGroupProviderDelegates`

**File:** `GameCore/Source/GameCore/Interfaces/GroupProvider.h` (same file)

An optional `UActorComponent` that provides a delegate-backed default implementation of `IGroupProvider`. Games that do not yet have a concrete group system, or that prefer loose binding over subclassing, add this component to `APlayerState` and bind the delegates from whatever system owns group data.

When a delegate is unbound, the corresponding method returns a safe solo fallback. When all delegates are bound, behaviour is identical to a fully implemented `IGroupProvider`.

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UGroupProviderDelegates : public UActorComponent
{
    GENERATED_BODY()
public:
    // Bind to return current group size.
    // Unbound fallback: returns 1 (solo).
    TDelegate<int32()> GetGroupSizeDelegate;

    // Bind to return whether this player is the group leader.
    // Unbound fallback: returns false.
    TDelegate<bool()> IsGroupLeaderDelegate;

    // Bind to populate OutMembers with all group members including this player.
    // Unbound fallback: OutMembers contains only the owning PlayerState.
    TDelegate<void(TArray<APlayerState*>&)> GetGroupMembersDelegate;

    // Bind to return the actor hosting USharedQuestCoordinator.
    // Unbound fallback: returns nullptr.
    TDelegate<AActor*()> GetGroupActorDelegate;

    // ── IGroupProvider forwarding helpers ───────────────────────────────────────
    // Called by APlayerState::IGroupProvider implementation to forward to delegates.

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
```

---

## Integration Patterns

### Option A — Delegate-based (no subclassing)

Add `UGroupProviderDelegates` to `APlayerState`. Implement `IGroupProvider` by forwarding to the component. Bind delegates from your group/party system.

```cpp
// AMyPlayerState.h
class AMyPlayerState : public APlayerState, public IGroupProvider
{
    GENERATED_BODY()
public:
    virtual int32 GetGroupSize() const override
        { return GroupProviderDelegates->ForwardGetGroupSize(); }
    virtual bool IsGroupLeader() const override
        { return GroupProviderDelegates->ForwardIsGroupLeader(); }
    virtual void GetGroupMembers(TArray<APlayerState*>& Out) const override
        { GroupProviderDelegates->ForwardGetGroupMembers(Out); }
    virtual AActor* GetGroupActor() const override
        { return GroupProviderDelegates->ForwardGetGroupActor(); }

    UPROPERTY()
    TObjectPtr<UGroupProviderDelegates> GroupProviderDelegates;
};

// In your party component's BeginPlay or initialization:
PlayerState->GroupProviderDelegates->GetGroupSizeDelegate.BindUObject(
    this, &UMyPartyComponent::GetMemberCount);
PlayerState->GroupProviderDelegates->IsGroupLeaderDelegate.BindUObject(
    this, &UMyPartyComponent::IsThisPlayerLeader);
PlayerState->GroupProviderDelegates->GetGroupMembersDelegate.BindUObject(
    this, &UMyPartyComponent::GetAllMemberPlayerStates);
PlayerState->GroupProviderDelegates->GetGroupActorDelegate.BindUObject(
    this, &UMyPartyComponent::GetPartyActor);
```

### Option B — Direct implementation

Implement `IGroupProvider` directly on `APlayerState` with real logic from your party system. `UGroupProviderDelegates` is not needed.

```cpp
class AMyPlayerState : public APlayerState, public IGroupProvider
{
    virtual int32 GetGroupSize() const override
        { return PartyComp ? PartyComp->GetMemberCount() : 1; }
    virtual bool IsGroupLeader() const override
        { return PartyComp && PartyComp->IsLeader(this); }
    virtual void GetGroupMembers(TArray<APlayerState*>& Out) const override
        { if (PartyComp) PartyComp->GetAllMembers(Out); else Out.Add(
            const_cast<AMyPlayerState*>(this)); }
    virtual AActor* GetGroupActor() const override
        { return PartyComp ? PartyComp->GetPartyActor() : nullptr; }
private:
    UPROPERTY()
    TObjectPtr<UMyPartyComponent> PartyComp;
};
```

---

## Fallback Behavior When Not Implemented

Systems that consume `IGroupProvider` always null-check the cast result:

```cpp
IGroupProvider* Provider = Cast<IGroupProvider>(GetOwner());
if (!Provider)
{
    // APlayerState does not implement IGroupProvider.
    // Fall back to solo behavior silently.
    GroupSize = 1;
    return;
}
```

This means `USharedQuestComponent` can be dropped onto `APlayerState` before any group system is implemented, and all quests behave as solo quests until `IGroupProvider` is wired up.

---

## File and Folder Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Interfaces/
            └── GroupProvider.h         ← IGroupProvider + UGroupProviderDelegates
                                          (no .cpp needed — all inline)
```

## Known Limitations

- `GetGroupMembers` output array is valid for the current frame only. Consumers must not cache it across frames or ticks.
- `IGroupProvider` has no notification mechanism — it is a polling interface. Systems that need to react to group membership changes should subscribe to group system events directly rather than polling through this interface.
