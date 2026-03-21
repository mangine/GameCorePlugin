# Group System — Usage

All player-facing mutations go through `ServerRPC_*` methods on `UGroupMemberComponent`. Server-only code (matchmaking, instanced content) calls `UGroupSubsystem` and `UGroupComponent` directly.

---

## Minimum Setup

**1. Create a `UGroupConfigDataAsset` asset** and configure it:

```
MaxGroupSize          = 5
MaxGroupsPerRaid      = 4
InviteExpirySeconds   = 30
DisconnectGraceSeconds= 60
SharedDataHeartbeatInterval = 1.0
GroupSharedDataClass  = (your subclass or none)
RaidSharedDataClass   = (your subclass or none)
```

**2. Assign it** in Project Settings → GameCore → `GroupConfig`.

**3. Add `UGroupMemberComponent` to your `APlayerState` subclass:**

```cpp
// MyPlayerState.h
UPROPERTY()
TObjectPtr<UGroupMemberComponent> GroupMemberComponent;
```

Create it in the constructor:
```cpp
GroupMemberComponent = CreateDefaultSubobject<UGroupMemberComponent>(TEXT("GroupMemberComponent"));
```

**4. Implement `IGroupProvider` on `APlayerState`** by forwarding to the component (see Wiring section below).

**5. Call disconnect/reconnect hooks from `AGameMode`:**

```cpp
void AMyGameMode::NotifyPlayerDisconnected(AGameModeBase* GameMode, APlayerController* PC)
{
    Super::NotifyPlayerDisconnected(GameMode, PC);
    if (APlayerState* PS = PC ? PC->GetPlayerState<APlayerState>() : nullptr)
    {
        if (UGroupSubsystem* GS = GetWorld()->GetSubsystem<UGroupSubsystem>())
            GS->OnPlayerDisconnected(PS);
    }
}

void AMyGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    if (APlayerState* PS = NewPlayer->GetPlayerState<APlayerState>())
    {
        if (UGroupSubsystem* GS = GetWorld()->GetSubsystem<UGroupSubsystem>())
            GS->OnPlayerReconnected(PS); // no-op if no grace entry found
    }
}
```

---

## Defining Shared Data

Subclass `UMemberSharedData` in your game module:

```cpp
// Game module — PirateGroupMemberData.h
UCLASS()
class UPirateGroupMemberData : public UMemberSharedData
{
    GENERATED_BODY()
public:
    UPROPERTY() FText CharacterName;
    UPROPERTY() int32 CurrentHP = 0;
    UPROPERTY() int32 MaxHP = 0;

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

---

## Wiring IGroupProvider on APlayerState

```cpp
// AMyPlayerState.h
class AMyPlayerState : public APlayerState, public IGroupProvider
{
    GENERATED_BODY()
public:
    virtual int32   GetGroupSize()                               const override;
    virtual bool    IsGroupLeader(const APlayerState* PS)        const override;
    virtual void    GetGroupMembers(TArray<APlayerState*>& Out)  const override;
    virtual AActor* GetGroupActor()                              const override;
    virtual AActor* GetRaidActor()                               const override;
    virtual void    GetRaidMembers(TArray<APlayerState*>& Out)   const override;

    UPROPERTY()
    TObjectPtr<UGroupMemberComponent> GroupMemberComponent;
};

// AMyPlayerState.cpp
int32 AMyPlayerState::GetGroupSize() const
    { return GroupMemberComponent->GroupProvider_GetGroupSize(); }
bool AMyPlayerState::IsGroupLeader(const APlayerState* PS) const
    { return PS == this && GroupMemberComponent->GroupProvider_IsGroupLeader(); }
void AMyPlayerState::GetGroupMembers(TArray<APlayerState*>& Out) const
    { GroupMemberComponent->GroupProvider_GetGroupMembers(Out); }
AActor* AMyPlayerState::GetGroupActor() const
    { return GroupMemberComponent->GroupProvider_GetGroupActor(); }
AActor* AMyPlayerState::GetRaidActor() const
    { return GroupMemberComponent->GroupProvider_GetRaidActor(); }
void AMyPlayerState::GetRaidMembers(TArray<APlayerState*>& Out) const
    { GroupMemberComponent->GroupProvider_GetRaidMembers(Out); }
```

---

## Sending a Group Invite (Player Input)

```cpp
// Client-side input handler. Validation is UI-only — real validation is server-side.
UGroupMemberComponent* GroupComp = GetPlayerState<AMyPlayerState>()->GroupMemberComponent;
GroupComp->ServerRPC_InvitePlayer(TargetPlayerState);
```

---

## Accepting / Declining an Invite (Player Input)

```cpp
// Accept
GroupComp->ServerRPC_AcceptInvite();

// Decline
GroupComp->ServerRPC_DeclineInvite();
```

---

## Leaving a Group

```cpp
GroupComp->ServerRPC_LeaveGroup();
// If the group drops to 1 member the group disbands automatically.
// If in a raid and this player is a group leader, the group is removed from the raid first.
```

---

## Kicking a Member

```cpp
// Only valid if this player is the group leader.
GroupComp->ServerRPC_KickMember(TargetPlayerState);
```

---

## Transferring Leadership

```cpp
GroupComp->ServerRPC_TransferLeadership(NewLeaderPlayerState);
```

---

## Raid Operations

```cpp
// Invite another group into the raid (caller must be raid leader or creating the raid).
GroupComp->ServerRPC_InviteGroupToRaid(TargetGroupLeaderPS);

// Accept a raid invite (called by the target group leader).
GroupComp->ServerRPC_AcceptRaidInvite();

// Remove own group from the raid.
GroupComp->ServerRPC_LeaveRaid();

// Kick a member from the raid (raid leader or assistant).
GroupComp->ServerRPC_KickMemberFromRaid(TargetPS);

// Promote a member to raid assistant (raid leader only).
GroupComp->ServerRPC_PromoteToRaidAssistant(TargetPS);
```

---

## Programmatic Group Formation (Matchmaking — Server Only)

```cpp
// Server-only — call from matchmaking subsystem after players are assigned.
UGroupSubsystem* GS = GetWorld()->GetSubsystem<UGroupSubsystem>();
AGroupActor* Group = GS->CreateGroup(LeaderPlayerState);
for (APlayerState* PS : OtherMembers)
{
    Group->GetGroupComponent()->ForceAddMember(PS);
}
```

---

## Reading Group State on the Client (UI)

```cpp
// Bind to the delegate for reactive UI updates.
UGroupMemberComponent* Comp = LocalPlayerState->GroupMemberComponent;
Comp->OnGroupViewChanged.AddUObject(this, &UGroupHUDWidget::OnGroupViewChanged);

// Or read the snapshot directly:
const FGroupMemberView& View = Comp->GetGroupView();
for (const FGroupMemberEntry& Entry : View.Members)
{
    if (UPirateGroupMemberData* Data = Cast<UPirateGroupMemberData>(Entry.GroupSharedData))
    {
        SetMemberHP(Entry.SlotIndex, Data->CurrentHP, Data->MaxHP);
    }
}

// Cross-group raid members:
for (const FGroupMemberEntry& Entry : View.RaidMembers)
{
    // Entry.GroupSharedData is always null for cross-group entries.
    // Only Entry.RaidSharedData is populated.
    AddRaidMemberToUI(Entry);
}
```

---

## Listening to Group Events

```cpp
// Example: quest system reacts to MemberJoined.
FGameplayMessageListenerHandle Handle = UGameCoreEventBus::Get(this)->StartListening<FGroupMemberChangedMessage>(
    GameCoreGroupTags::MemberJoined,
    [this](FGameplayTag, const FGroupMemberChangedMessage& Msg)
    {
        UpdateQuestGroupRequirement(Msg.GroupActor);
    });

// Store Handle and StopListening in EndPlay.
```

---

## Checking Group State via IGroupProvider (Other Systems)

```cpp
// Quest system, requirement system, etc. use this path — no Group module dependency.
if (IGroupProvider* Provider = Cast<IGroupProvider>(PlayerState))
{
    int32 GroupSize = Provider->GetGroupSize();
    bool  bIsLeader = Provider->IsGroupLeader(PlayerState);

    TArray<APlayerState*> Members;
    Provider->GetGroupMembers(Members);
}
```
