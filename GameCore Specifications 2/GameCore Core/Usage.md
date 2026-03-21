# GameCore Core — Usage

**Part of:** GameCore Plugin | **Module:** `GameCore` | **UE Version:** 5.7

---

## ISourceIDInterface

### Implementing on an Actor or Component

Any `UObject` can implement `ISourceIDInterface` to declare itself as an identifiable source. The tag must follow the `Source.*` hierarchy.

```cpp
// MyMobActor.h
#include "Core/SourceID/SourceIDInterface.h"

UCLASS()
class AMyMobActor : public ACharacter, public ISourceIDInterface
{
    GENERATED_BODY()
public:
    virtual FGameplayTag GetSourceTag_Implementation() const override
    {
        // Derive from existing actor data — do not store a redundant tag field
        return MobData ? MobData->SourceTag : FGameplayTag::EmptyTag;
    }

    virtual FText GetSourceDisplayName_Implementation() const override
    {
        return FText::FromString(GetName()); // optional — CS/debug tooling only
    }

private:
    UPROPERTY()
    TObjectPtr<UMobDataAsset> MobData;
};
```

### Consuming in a System

Systems that need source provenance accept `TScriptInterface<ISourceIDInterface>` and null-check before use.

```cpp
// In any system (XP, Drops, Market...)
void UMyProgressionComponent::AddXP(
    FGameplayTag ProgressionTag,
    int32 Amount,
    TScriptInterface<ISourceIDInterface> Source)
{
    // ... apply XP logic ...

    // Audit trail — only if a source was provided
    if (Source.GetObject())
    {
        FGameplayTag SourceTag  = Source->GetSourceTag();
        FText        SourceName = Source->GetSourceDisplayName();
        // Forward to backend telemetry, audit log, etc.
        UE_LOG(LogProgression, Log, TEXT("XP granted from %s (%s)"),
            *SourceTag.ToString(), *SourceName.ToString());
    }
}
```

### Passing a Source at a Call Site

```cpp
// Granting XP from a mob kill — mob actor implements ISourceIDInterface
AMyMobActor* Mob = /* ... */;
ProgressionComp->AddXP(
    GameplayTags::Progression_Combat,
    100,
    TScriptInterface<ISourceIDInterface>(Mob));

// Granting XP from a system with no source (login bonus)
ProgressionComp->AddXP(
    GameplayTags::Progression_Login,
    50,
    nullptr); // Source is optional — null is always valid
```

---

## IGroupProvider

### Option A — Delegate-Based (no APlayerState subclassing)

Add `UGroupProviderDelegates` to `APlayerState`. Implement `IGroupProvider` by forwarding to the component. Bind delegates from your party/group system.

```cpp
// MyPlayerState.h
#include "Interfaces/GroupProvider.h"

UCLASS()
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
```

```cpp
// MyPartyComponent.cpp — BeginPlay or initialization
void UMyPartyComponent::BeginPlay()
{
    Super::BeginPlay();

    AMyPlayerState* PS = GetOwner<AMyPlayerState>();
    if (!ensure(PS)) return;

    PS->GroupProviderDelegates->GetGroupSizeDelegate.BindUObject(
        this, &UMyPartyComponent::GetMemberCount);
    PS->GroupProviderDelegates->IsGroupLeaderDelegate.BindUObject(
        this, &UMyPartyComponent::IsThisPlayerLeader);
    PS->GroupProviderDelegates->GetGroupMembersDelegate.BindUObject(
        this, &UMyPartyComponent::GetAllMemberPlayerStates);
    PS->GroupProviderDelegates->GetGroupActorDelegate.BindUObject(
        this, &UMyPartyComponent::GetPartyActor);
}
```

### Option B — Direct Implementation

Implement `IGroupProvider` directly with real logic from your party system. `UGroupProviderDelegates` is not needed.

```cpp
class AMyPlayerState : public APlayerState, public IGroupProvider
{
    GENERATED_BODY()
public:
    virtual int32 GetGroupSize() const override
        { return PartyComp ? PartyComp->GetMemberCount() : 1; }
    virtual bool IsGroupLeader() const override
        { return PartyComp && PartyComp->IsLeader(this); }
    virtual void GetGroupMembers(TArray<APlayerState*>& Out) const override
    {
        if (PartyComp) PartyComp->GetAllMembers(Out);
        else Out.Add(const_cast<AMyPlayerState*>(this));
    }
    virtual AActor* GetGroupActor() const override
        { return PartyComp ? PartyComp->GetPartyActor() : nullptr; }
private:
    UPROPERTY()
    TObjectPtr<UMyPartyComponent> PartyComp;
};
```

### Consuming IGroupProvider in a System

Always null-check the cast. A missing `IGroupProvider` implementation means solo behavior — this is a valid state during early development.

```cpp
void USharedQuestComponent::OnEnrollmentRequested()
{
    IGroupProvider* Provider = Cast<IGroupProvider>(GetOwner());
    if (!Provider)
    {
        // APlayerState does not implement IGroupProvider yet.
        // Treat as solo — quest functions with group size 1.
        EnrollSolo();
        return;
    }

    int32 GroupSize = Provider->GetGroupSize();
    bool  bIsLeader = Provider->IsGroupLeader();

    TArray<APlayerState*> Members;
    Provider->GetGroupMembers(Members); // Output valid this frame only — do not cache

    if (bIsLeader)
        BroadcastEnrollmentToGroup(Members);
}
```

### Important: GetGroupMembers Output Lifetime

```cpp
// CORRECT — use the array immediately, local scope only
{
    TArray<APlayerState*> Members;
    Provider->GetGroupMembers(Members);
    for (APlayerState* PS : Members)
    {
        // Process PS immediately
    }
} // Members goes out of scope here — correct

// WRONG — do not store the array as a member or across frames
CachedMembers.Reset();
Provider->GetGroupMembers(CachedMembers); // CachedMembers may contain stale pointers next frame
```
