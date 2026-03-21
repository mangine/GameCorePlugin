# UFactionComponent

**File:** `Factions/FactionComponent.h / .cpp`

The actor component attached to any player or NPC that participates in the faction system. Owns the membership list, local relationship overrides, and all mutation methods. Mutation is **server-only**. Membership data replicates delta-compressed to clients via `FFastArraySerializer`.

---

## Class Definition

```cpp
/**
 * UFactionComponent
 *
 * Attach to any actor (player or NPC) that participates in faction logic.
 *
 * Mutation methods (JoinFaction, LeaveFaction, SetRank) are server-only.
 * Query methods (GetRelationshipTo, IsMemberOf, GetFactionTags) are safe on server and client.
 *
 * Memberships replicate delta-compressed via FFastArraySerializer.
 * LocalOverrides replicates in full (expected 0–3 entries max).
 * FallbackRelationship replicates in full.
 */
UCLASS(ClassGroup=(GameCore),
    meta=(BlueprintSpawnableComponent, DisplayName="Faction Component"))
class GAMECORE_API UFactionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFactionComponent();

    // ── Config ────────────────────────────────────────────────────────────

    // Used when this component has no primary memberships.
    // Resolution when both actors have no primaries: FMath::Min(Source.Fallback, Target.Fallback).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Factions")
    EFactionRelationship FallbackRelationship = EFactionRelationship::Neutral;

    // ── Memberships ───────────────────────────────────────────────────────

    // Replicated via FFastArraySerializer — delta-compressed per cycle.
    // For NPCs: populate in Blueprint defaults.
    // For players: populated at runtime by game module (loaded from save data).
    UPROPERTY(Replicated)
    FFactionMembershipArray Memberships;

    // Per-entity overrides checked before the global subsystem cache.
    // Use sparingly: bounty hunters, story NPCs with special standing.
    // Replicated in full. Expected to be 0–3 entries.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Factions")
    TArray<FFactionRelationshipOverride> LocalOverrides;

    // ── Delegates ─────────────────────────────────────────────────────────

    // Broadcast locally (server and owning client) after any join or leave.
    // For game module wiring — external systems should use the event bus.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
        FOnFactionMembershipChanged,
        FGameplayTag, FactionTag,
        bool, bJoined);
    UPROPERTY(BlueprintAssignable)
    FOnFactionMembershipChanged OnMembershipChanged;

    // Broadcast after SetRank succeeds. For game module reputation wiring.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
        FOnFactionRankChanged,
        FGameplayTag, FactionTag,
        FGameplayTag, NewRankTag);
    UPROPERTY(BlueprintAssignable)
    FOnFactionRankChanged OnRankChanged;

    // ── Mutation — Server Only ────────────────────────────────────────────

    // Evaluates JoinRequirements from UFactionDefinition, adds the membership,
    // broadcasts event bus + OnMembershipChanged.
    // Returns false if requirements fail; OutFailureReason is populated.
    // Idempotent: returns true if already a member.
    // Authority guard: logs error and returns false on client.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool JoinFaction(FGameplayTag FactionTag, FText& OutFailureReason,
        bool bPrimary = true);

    // Removes membership. Broadcasts event bus + OnMembershipChanged.
    // No-op and returns false if not a member.
    // Authority: server only.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool LeaveFaction(FGameplayTag FactionTag);

    // Sets the rank tag for an existing membership.
    // RankTag must be in UFactionDefinition::RankTags, or empty.
    // Validated via ensure() in non-shipping builds.
    // Authority: server only.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool SetRank(FGameplayTag FactionTag, FGameplayTag RankTag);

    // ── Queries — Safe on Server and Client ───────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool IsMemberOf(FGameplayTag FactionTag) const;

    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool GetMembership(FGameplayTag FactionTag,
        FFactionMembership& OutMembership) const;

    // Resolves the worst relationship toward another component.
    // Delegates entirely to UFactionSubsystem::GetActorRelationship.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    EFactionRelationship GetRelationshipTo(
        const UFactionComponent* Other) const;

    // Fills OutTags with faction tags on this component.
    // bPrimaryOnly = true: only primary memberships.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    void GetFactionTags(FGameplayTagContainer& OutTags,
        bool bPrimaryOnly = false) const;

    // ── UActorComponent ───────────────────────────────────────────────────

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
```

---

## Method Implementations

### `JoinFaction`

```cpp
bool UFactionComponent::JoinFaction(
    FGameplayTag FactionTag, FText& OutFailureReason, bool bPrimary)
{
    if (!GetOwner()->HasAuthority())
    {
        UE_LOG(LogFaction, Error,
            TEXT("UFactionComponent::JoinFaction called on client. Server only."));
        return false;
    }

    if (IsMemberOf(FactionTag))
        return true; // Idempotent

    const UFactionSubsystem* FS = GetWorld()->GetSubsystem<UFactionSubsystem>();
    const UFactionDefinition* Def = FS ? FS->GetDefinition(FactionTag) : nullptr;

    // Evaluate join requirements.
    if (Def && !Def->JoinRequirements.IsEmpty())
    {
        FRequirementContext Context;
        Context.World      = GetWorld();
        Context.Instigator = GetOwner();
        if (APawn* Pawn = Cast<APawn>(GetOwner()))
            Context.PlayerState = Pawn->GetPlayerState();

        for (const TObjectPtr<URequirement>& Req : Def->JoinRequirements)
        {
            if (!Req) continue;
            FRequirementResult Result = Req->Evaluate(Context);
            if (!Result.bPassed)
            {
                OutFailureReason = Result.FailureReason;
                return false;
            }
        }
    }

    FFactionMembership& NewMembership = Memberships.Items.AddDefaulted_GetRef();
    NewMembership.FactionTag = FactionTag;
    NewMembership.Faction    = Def; // Null for bare secondary memberships
    NewMembership.bPrimary   = bPrimary;
    Memberships.MarkItemDirty(NewMembership);

    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FFactionMembershipChangedMessage Msg;
        Msg.Actor      = GetOwner();
        Msg.FactionTag = FactionTag;
        Bus->Broadcast(GameCoreEventTags::Faction_MemberJoined, Msg,
            EGameCoreEventScope::ServerOnly);
    }

    OnMembershipChanged.Broadcast(FactionTag, true);
    return true;
}
```

### `LeaveFaction`

```cpp
bool UFactionComponent::LeaveFaction(FGameplayTag FactionTag)
{
    if (!GetOwner()->HasAuthority()) return false;

    for (int32 i = 0; i < Memberships.Items.Num(); ++i)
    {
        if (Memberships.Items[i].FactionTag != FactionTag) continue;

        Memberships.Items.RemoveAtSwap(i);
        Memberships.MarkArrayDirty();

        if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        {
            FFactionMembershipChangedMessage Msg;
            Msg.Actor      = GetOwner();
            Msg.FactionTag = FactionTag;
            Bus->Broadcast(GameCoreEventTags::Faction_MemberLeft, Msg,
                EGameCoreEventScope::ServerOnly);
        }

        OnMembershipChanged.Broadcast(FactionTag, false);
        return true;
    }
    return false;
}
```

### `SetRank`

```cpp
bool UFactionComponent::SetRank(
    FGameplayTag FactionTag, FGameplayTag RankTag)
{
    if (!GetOwner()->HasAuthority()) return false;

    for (FFactionMembership& M : Memberships.Items)
    {
        if (M.FactionTag != FactionTag) continue;

#if !UE_BUILD_SHIPPING
        if (const UFactionSubsystem* FS = GetWorld()->GetSubsystem<UFactionSubsystem>())
        {
            if (const UFactionDefinition* Def = FS->GetDefinition(FactionTag))
            {
                ensure(!RankTag.IsValid() || Def->RankTags.Contains(RankTag));
            }
        }
#endif

        M.RankTag = RankTag;
        Memberships.MarkItemDirty(M);

        if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        {
            FFactionMembershipChangedMessage Msg;
            Msg.Actor      = GetOwner();
            Msg.FactionTag = FactionTag;
            Msg.NewRankTag = RankTag;
            Bus->Broadcast(GameCoreEventTags::Faction_RankChanged, Msg,
                EGameCoreEventScope::ServerOnly);
        }

        OnRankChanged.Broadcast(FactionTag, RankTag);
        return true;
    }
    return false;
}
```

### `GetRelationshipTo`

```cpp
EFactionRelationship UFactionComponent::GetRelationshipTo(
    const UFactionComponent* Other) const
{
    if (!Other) return EFactionRelationship::Neutral;

    if (const UFactionSubsystem* FS =
        GetWorld()->GetSubsystem<UFactionSubsystem>())
    {
        return FS->GetActorRelationship(this, Other);
    }
    return EFactionRelationship::Neutral;
}
```

> All resolution logic lives in `UFactionSubsystem`. The component delegates entirely — no resolution logic belongs here.

### `IsMemberOf` / `GetMembership` / `GetFactionTags`

```cpp
bool UFactionComponent::IsMemberOf(FGameplayTag FactionTag) const
{
    for (const FFactionMembership& M : Memberships.Items)
        if (M.FactionTag == FactionTag) return true;
    return false;
}

bool UFactionComponent::GetMembership(FGameplayTag FactionTag,
    FFactionMembership& OutMembership) const
{
    for (const FFactionMembership& M : Memberships.Items)
    {
        if (M.FactionTag == FactionTag)
        {
            OutMembership = M;
            return true;
        }
    }
    return false;
}

void UFactionComponent::GetFactionTags(
    FGameplayTagContainer& OutTags, bool bPrimaryOnly) const
{
    for (const FFactionMembership& M : Memberships.Items)
    {
        if (bPrimaryOnly && !M.bPrimary) continue;
        if (M.FactionTag.IsValid())
            OutTags.AddTag(M.FactionTag);
    }
}
```

### `BeginPlay`

```cpp
void UFactionComponent::BeginPlay()
{
    Super::BeginPlay();

#if !UE_BUILD_SHIPPING
    if (const UFactionSubsystem* FS =
        GetWorld()->GetSubsystem<UFactionSubsystem>())
    {
        for (const FFactionMembership& M : Memberships.Items)
        {
            if (!M.FactionTag.IsValid() || !M.bPrimary) continue;
            if (!FS->GetDefinition(M.FactionTag))
            {
                UE_LOG(LogFaction, Warning,
                    TEXT("UFactionComponent on [%s]: primary FactionTag [%s] "
                         "has no registered UFactionDefinition. "
                         "Add it to UFactionRelationshipTable."),
                    *GetOwner()->GetName(),
                    *M.FactionTag.ToString());
            }
        }
    }
#endif
}
```

---

## Replication

```cpp
void UFactionComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UFactionComponent, Memberships);
    DOREPLIFETIME(UFactionComponent, LocalOverrides);
    DOREPLIFETIME(UFactionComponent, FallbackRelationship);
}
```

`Memberships` is delta-compressed — only changed items are sent per cycle. `LocalOverrides` and `FallbackRelationship` replicate in full but are expected to be small and rarely changing.

---

## Integration Notes

**AI targeting:** Call `UFactionSubsystem::GetHostileFactions` once per AI actor at spawn. Cache the result. Refresh the cache when `GameCoreEvent.Faction.MemberJoined` or `MemberLeft` fires for that actor.

**Interaction gating:** Add `URequirement_FactionCompatibility` to `FInteractionEntryConfig::EntryRequirements` to gate interactions by faction relationship.

**Checking rank in requirements:** Create a game-module `URequirement` subclass that reads `GetMembership().RankTag` from `Context.Instigator`'s `UFactionComponent` and compares it against a rank index in `UFactionDefinition::RankTags`. No new GameCore API needed.

**Player save/restore:** Call `JoinFaction` and `SetRank` on the server during player login to restore membership from save data. The Faction System does not auto-save.
