# AGroupActor

**File:** `Group/GroupActor.h` / `GroupActor.cpp`
**Authority:** Server only

Server-side owner of one group. Implements `IGroupProvider` so that Quest, Requirement, and other systems can consume group data without a hard dependency on the Group module. Delegates all member management to `UGroupComponent`.

---

## Class Declaration

```cpp
UCLASS(NotBlueprintable)
class GAMECORE_API AGroupActor : public AActor, public IGroupProvider
{
    GENERATED_BODY()
public:
    AGroupActor();

    UGroupComponent* GetGroupComponent() const { return GroupComponent; }

    // Returns the raid this group belongs to, or nullptr.
    ARaidActor* GetOwningRaid() const { return OwningRaid.Get(); }

    // ── IGroupProvider ──────────────────────────────────────────────────────
    virtual int32   GetGroupSize()                                  const override;
    virtual bool    IsGroupLeader(const APlayerState* PS)           const override;
    virtual void    GetGroupMembers(TArray<APlayerState*>& Out)     const override;
    virtual AActor* GetGroupActor()                                 const override { return const_cast<AGroupActor*>(this); }
    virtual AActor* GetRaidActor()                                  const override;
    virtual void    GetRaidMembers(TArray<APlayerState*>& Out)      const override;

private:
    UPROPERTY()
    TObjectPtr<UGroupComponent> GroupComponent;

    // Backref to the raid this group belongs to. Null if not in a raid.
    // Set/cleared by URaidComponent via the friend declaration.
    TWeakObjectPtr<ARaidActor> OwningRaid;

    friend class URaidComponent;
};
```

---

## Constructor

```cpp
AGroupActor::AGroupActor()
{
    // Server-only actor. Never replicate.
    bReplicates       = false;
    bNetLoadOnClient  = false;
    PrimaryActorTick.bCanEverTick = false;

    GroupComponent = CreateDefaultSubobject<UGroupComponent>(TEXT("GroupComponent"));
}
```

---

## IGroupProvider Implementations

```cpp
int32 AGroupActor::GetGroupSize() const
{
    return GroupComponent->GetMemberCount();
}

bool AGroupActor::IsGroupLeader(const APlayerState* PS) const
{
    return GroupComponent->GetLeader() == PS;
}

void AGroupActor::GetGroupMembers(TArray<APlayerState*>& Out) const
{
    GroupComponent->GetAllMembers(Out);
}

AActor* AGroupActor::GetRaidActor() const
{
    return OwningRaid.Get();
}

void AGroupActor::GetRaidMembers(TArray<APlayerState*>& Out) const
{
    ARaidActor* Raid = OwningRaid.Get();
    if (Raid)
        Raid->GetRaidComponent()->GetAllRaidMembers(Out);
}
```

---

## Implementation Notes

- `bReplicates = false` and `bNetLoadOnClient = false` are mandatory. This actor must never appear on a client.
- `PrimaryActorTick.bCanEverTick = false` — ticking is done by `UGroupComponent`.
- `OwningRaid` is a `TWeakObjectPtr` to avoid keeping a raid alive after dissolve.
- The `friend class URaidComponent` declaration is the only way `URaidComponent` can set `OwningRaid` without a public setter. This is intentional — only the raid component manages group-to-raid membership.
