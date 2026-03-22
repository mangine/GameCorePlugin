#include "ZoneTrackerComponent.h"
#include "Zone/ZoneActor.h"
#include "Zone/ZoneSubsystem.h"
#include "Zone/ZoneMessages.h"
#include "Zone/ZoneChannelTags.h"
#include "EventBus/GameCoreEventBus.h"
#include "StructUtils/InstancedStruct.h"

UZoneTrackerComponent::UZoneTrackerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UZoneTrackerComponent::BeginPlay()
{
    Super::BeginPlay();
    // CurrentZones starts empty. First EvaluateZones() fires bEntered=true
    // for all zones the actor is already inside — intentional.
}

void UZoneTrackerComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Func)
{
    Super::TickComponent(DeltaTime, TickType, Func);

    TimeSinceLastQuery += DeltaTime;
    if (TimeSinceLastQuery >= QueryInterval)
    {
        TimeSinceLastQuery = 0.f;
        EvaluateZones();
    }
}

bool UZoneTrackerComponent::IsInZoneOfType(FGameplayTag TypeTag) const
{
    for (const AZoneActor* Zone : CurrentZones)
    {
        if (Zone && Zone->DataAsset &&
            Zone->DataAsset->ZoneTypeTag.MatchesTagExact(TypeTag))
            return true;
    }
    return false;
}

void UZoneTrackerComponent::EvaluateZones()
{
    UZoneSubsystem* Sub = GetWorld()->GetSubsystem<UZoneSubsystem>();
    if (!Sub) return;

    const FVector Location = GetQueryLocation();
    TArray<AZoneActor*> NewZones = Sub->QueryZonesAtPoint(Location);

    // Exits: in CurrentZones but not in NewZones
    for (AZoneActor* Zone : CurrentZones)
    {
        if (!NewZones.Contains(Zone))
            BroadcastTransition(Zone, /*bEntered=*/false);
    }

    // Enters: in NewZones but not in CurrentZones
    for (AZoneActor* Zone : NewZones)
    {
        if (!CurrentZones.Contains(Zone))
            BroadcastTransition(Zone, /*bEntered=*/true);
    }

    CurrentZones = MoveTemp(NewZones);
}

void UZoneTrackerComponent::BroadcastTransition(AZoneActor* Zone, bool bEntered) const
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FZoneTransitionMessage Msg;
        Msg.TrackedActor = GetOwner();
        Msg.ZoneActor    = Zone;
        Msg.StaticData   = Zone->DataAsset;
        Msg.DynamicState = Zone->DynamicState;
        Msg.bEntered     = bEntered;
        Bus->Broadcast(GameCore::Zone::Tags::Channel_Transition,
            FInstancedStruct::Make(Msg),
            EGameCoreEventScope::Both);
    }
}

FVector UZoneTrackerComponent::GetQueryLocation() const
{
    if (LocationAnchor)
        return LocationAnchor->GetComponentLocation();
    return GetOwner()->GetActorLocation();
}
