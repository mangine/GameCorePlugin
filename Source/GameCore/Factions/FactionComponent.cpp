// Copyright GameCore Plugin. All Rights Reserved.
#include "FactionComponent.h"
#include "FactionSubsystem.h"
#include "FactionDefinition.h"
#include "Factions/Requirements/Requirement_FactionCompatibility.h"
#include "EventBus/GameCoreEventBus.h"
#include "Requirements/Requirement.h"
#include "Requirements/RequirementContext.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

#define LOCTEXT_NAMESPACE "FactionSystem"

UFactionComponent::UFactionComponent()
{
    SetIsReplicatedByDefault(true);
}

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
    // Context is packed as FFactionRequirementContext inside FRequirementContext::Data.
    if (Def && !Def->JoinRequirements.IsEmpty())
    {
        FFactionRequirementContext FactionCtx;
        FactionCtx.Instigator = GetOwner();
        FactionCtx.World      = GetWorld();

        FRequirementContext Context = FRequirementContext::Make<FFactionRequirementContext>(FactionCtx);

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

void UFactionComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UFactionComponent, Memberships);
    DOREPLIFETIME(UFactionComponent, LocalOverrides);
    DOREPLIFETIME(UFactionComponent, FallbackRelationship);
}

#undef LOCTEXT_NAMESPACE
