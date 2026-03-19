# UFactionComponent & URequirement_FactionCompatibility

**Sub-page of:** [Faction System](../Faction%20System.md)

---

## `UFactionComponent`

**File:** `Factions/FactionComponent.h / .cpp`

The actor component attached to any player or NPC that participates in the faction system. Owns the membership list, local relationship overrides, and all mutation methods. Mutation is server-only; membership data replicates to clients via `FFastArraySerializer`.

```cpp
UCLASS(ClassGroup=(GameCore),
    meta=(BlueprintSpawnableComponent, DisplayName="Faction Component"))
class GAMECORE_API UFactionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFactionComponent();

    // ── Config ────────────────────────────────────────────────────────────

    // Used when this component has no primary memberships and is queried as
    // Source OR Target. Resolution: FMath::Min(Source.Fallback, Target.Fallback).
    // Defaults to Neutral.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Factions")
    EFactionRelationship FallbackRelationship = EFactionRelationship::Neutral;

    // ── Memberships ───────────────────────────────────────────────────────

    // Replicated via FFastArraySerializer. Delta-compressed — only changed items
    // are sent each cycle.
    // For NPCs: populate in Blueprint defaults.
    // For players: populated at runtime by game module (loaded from save data).
    UPROPERTY(Replicated)
    FFactionMembershipArray Memberships;

    // Per-entity overrides checked before the global subsystem cache.
    // Use sparingly: bounty hunters, story NPCs with special standing.
    // Replicated in full (expected to be small — 0–3 entries max).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Factions")
    TArray<FFactionRelationshipOverride> LocalOverrides;

    // ── Delegates ─────────────────────────────────────────────────────────

    // Broadcast locally (server and owning client) after any membership change.
    // Used by game module wiring — e.g. reputation rank advancement.
    // NOT a substitute for GMS events — external systems should use GMS.
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

    // Validates JoinRequirements from UFactionDefinition, adds the membership,
    // broadcasts GMS event + OnMembershipChanged delegate.
    // Returns false if requirements fail; OutFailureReason is populated.
    // No-op (returns true) if already a member.
    // Authority: server only. Logs an error and returns false on client.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool JoinFaction(FGameplayTag FactionTag, FText& OutFailureReason,
        bool bPrimary = true);

    // Removes membership. Broadcasts GMS event + OnMembershipChanged delegate.
    // No-op if not a member.
    // Authority: server only.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool LeaveFaction(FGameplayTag FactionTag);

    // Sets the rank tag for an existing membership.
    // RankTag must be a member of UFactionDefinition::RankTags, or empty.
    // Validated in non-shipping builds.
    // Authority: server only.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool SetRank(FGameplayTag FactionTag, FGameplayTag RankTag);

    // ── Queries — Safe on Server and Client ──────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool IsMemberOf(FGameplayTag FactionTag) const;

    UFUNCTION(BlueprintCallable, Category = "Factions")
    bool GetMembership(FGameplayTag FactionTag,
        FFactionMembership& OutMembership) const;

    // Resolves the worst relationship toward another component.
    // Applies LocalOverrides from both sides, then falls through to
    // UFactionSubsystem::GetActorRelationship.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    EFactionRelationship GetRelationshipTo(
        const UFactionComponent* Other) const;

    // Fills OutTags with all faction tags on this component.
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

## Key Method Implementations

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
        return true; // Already a member — idempotent

    const UFactionSubsystem* FS =
        GetWorld()->GetSubsystem<UFactionSubsystem>();
    const UFactionDefinition* Def =
        FS ? FS->GetDefinition(FactionTag) : nullptr;

    // Validate join requirements if definition exists.
    if (Def && !Def->JoinRequirements.IsEmpty())
    {
        FRequirementContext Context;
        Context.World = GetWorld();
        Context.PlayerState = GetOwner()->FindComponentByClass<UPawnComponent>()
            ? Cast<APawn>(GetOwner())->GetPlayerState() : nullptr;
        Context.Instigator = GetOwner();

        for (const TObjectPtr<URequirement>& Req : Def->JoinRequirements)
        {
            FRequirementResult Result = Req->Evaluate(Context);
            if (!Result.bPassed)
            {
                OutFailureReason = Result.FailureReason;
                return false;
            }
        }
    }

    // Add membership.
    FFactionMembership& NewMembership = Memberships.Items.AddDefaulted_GetRef();
    NewMembership.FactionTag = FactionTag;
    NewMembership.Faction    = Def; // may be null for secondary bare memberships
    NewMembership.bPrimary   = bPrimary;
    Memberships.MarkItemDirty(NewMembership);

    // GMS broadcast.
    if (UGameCoreEventSubsystem* Bus =
        GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
    {
        FFactionMembershipChangedMessage Msg;
        Msg.Actor      = GetOwner();
        Msg.FactionTag = FactionTag;
        Bus->Broadcast(GameCoreEventTags::Faction_MemberJoined, Msg);
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
        if (Memberships.Items[i].FactionTag == FactionTag)
        {
            Memberships.Items.RemoveAtSwap(i);
            Memberships.MarkArrayDirty();

            if (UGameCoreEventSubsystem* Bus =
                GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
            {
                FFactionMembershipChangedMessage Msg;
                Msg.Actor      = GetOwner();
                Msg.FactionTag = FactionTag;
                Bus->Broadcast(GameCoreEventTags::Faction_MemberLeft, Msg);
            }

            OnMembershipChanged.Broadcast(FactionTag, false);
            return true;
        }
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
        // Validate rank tag is in the faction's declared rank list.
        if (const UFactionSubsystem* FS =
            GetWorld()->GetSubsystem<UFactionSubsystem>())
        {
            if (const UFactionDefinition* Def = FS->GetDefinition(FactionTag))
            {
                ensure(RankTag.IsValid() == false ||
                    Def->RankTags.Contains(RankTag));
            }
        }
#endif

        M.RankTag = RankTag;
        Memberships.MarkItemDirty(M);

        if (UGameCoreEventSubsystem* Bus =
            GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
        {
            FFactionMembershipChangedMessage Msg;
            Msg.Actor      = GetOwner();
            Msg.FactionTag = FactionTag;
            Msg.NewRankTag = RankTag;
            Bus->Broadcast(GameCoreEventTags::Faction_RankChanged, Msg);
        }

        OnRankChanged.Broadcast(FactionTag, RankTag);
        return true;
    }
    return false; // Not a member
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

The subsystem owns the full resolution logic (local overrides, cache, default fallback). The component delegates entirely — no resolution logic lives here.

### `BeginPlay`

```cpp
void UFactionComponent::BeginPlay()
{
    Super::BeginPlay();

#if !UE_BUILD_SHIPPING
    // Validate that all membership FactionTags are registered in the subsystem.
    if (const UFactionSubsystem* FS =
        GetWorld()->GetSubsystem<UFactionSubsystem>())
    {
        for (const FFactionMembership& M : Memberships.Items)
        {
            if (!M.FactionTag.IsValid()) continue;
            if (M.bPrimary && !FS->GetDefinition(M.FactionTag))
            {
                UE_LOG(LogFaction, Warning,
                    TEXT("UFactionComponent on [%s]: primary faction tag [%s] "
                         "has no registered UFactionDefinition. "
                         "Add it to the UFactionRelationshipTable."),
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

`Memberships` replicates via `FFastArraySerializer` — only changed items are sent. `LocalOverrides` replicates in full but is expected to be very small (0–3 entries). `FallbackRelationship` rarely changes; replicated in full.

---

## `URequirement_FactionCompatibility`

**File:** `Factions/Requirements/Requirement_FactionCompatibility.h / .cpp`

A `URequirement` subclass that validates an actor's existing primary faction memberships are compatible with a target faction. Used in `UFactionDefinition::JoinRequirements` to enforce mutual-exclusion constraints.

The threshold is configurable per faction — hostile factions are the common case, but a faction requiring Ally-level standing before admission is equally expressible.

```cpp
UCLASS(EditInlineNew, CollapseCategories,
    meta=(DisplayName="Faction Compatibility"))
class GAMECORE_API URequirement_FactionCompatibility : public URequirement
{
    GENERATED_BODY()
public:

    // The faction whose join requirements include this check.
    // Resolved at evaluation time via UFactionSubsystem.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Requirement")
    FGameplayTag TargetFactionTag;

    // The minimum relationship the joining actor's existing primary factions
    // must have toward TargetFactionTag.
    // Default: Unfriendly — blocks join only if a current faction is Hostile.
    // Set to Neutral to require no conflict at all.
    // Set to Ally to require existing ally status (e.g. prestige unlock).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Requirement")
    EFactionRelationship MinimumAllowedRelationship =
        EFactionRelationship::Unfriendly;

    // Data only exists on the server (UFactionSubsystem is authoritative).
    // Must not appear in ClientOnly or ClientValidated requirement sets.
    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        return ERequirementDataAuthority::ServerOnly;
    }

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        if (!Context.Instigator) return FRequirementResult::Fail();

        UFactionComponent* FC =
            Context.Instigator->FindComponentByClass<UFactionComponent>();
        if (!FC) return FRequirementResult::Pass(); // No factions = no conflict

        const UFactionSubsystem* FS =
            Context.World
            ? Context.World->GetSubsystem<UFactionSubsystem>()
            : nullptr;
        if (!FS) return FRequirementResult::Pass();

        for (const FFactionMembership& M : FC->Memberships.Items)
        {
            if (!M.bPrimary) continue;

            EFactionRelationship Rel =
                FS->GetRelationship(M.FactionTag, TargetFactionTag);

            if ((uint8)Rel < (uint8)MinimumAllowedRelationship)
            {
                return FRequirementResult::Fail(
                    FText::Format(
                        LOCTEXT("FactionConflict",
                            "Your membership in {0} conflicts with joining this faction."),
                        FS->GetDefinition(M.FactionTag)
                            ? FS->GetDefinition(M.FactionTag)->DisplayName
                            : FText::FromName(M.FactionTag.GetTagName())));
            }
        }
        return FRequirementResult::Pass();
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(
            TEXT("FactionCompatibility: existing primaries >= %s toward %s"),
            *StaticEnum<EFactionRelationship>()->GetNameStringByValue(
                (int64)MinimumAllowedRelationship),
            *TargetFactionTag.ToString());
    }
#endif
};
```

---

## Integration Notes

**AI targeting:** AI perception systems can call `GetRelationshipTo` on two components to decide combat stance. Alternatively they can call `UFactionSubsystem::GetHostileFactions` once per AI actor at spawn and cache the result, refreshing on `GameCoreEvent.Faction.MemberJoined` / `MemberLeft`.

**Interaction gating:** Add `URequirement_FactionCompatibility` (or a custom `URequirement` checking `GetRelationshipTo`) to an `FInteractionEntryConfig::EntryRequirements` array to gate interactions by faction relationship.

**Checking rank in requirements:** Use a game-module requirement subclass that reads `GetMembership().RankTag` from `Context.Instigator`'s `UFactionComponent` and compares it against an `UFactionDefinition::RankTags` index. No new GameCore API is needed.

**Loading player faction state:** The game module calls `JoinFaction` during player login to restore membership from save data. No auto-save occurs here — the game module owns persistence.
