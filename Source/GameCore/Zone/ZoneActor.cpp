#include "ZoneActor.h"
#include "Net/UnrealNetwork.h"
#include "Zone/ZoneSubsystem.h"
#include "Zone/ZoneMessages.h"
#include "Zone/ZoneChannelTags.h"
#include "EventBus/GameCoreEventBus.h"
#include "StructUtils/InstancedStruct.h"

AZoneActor::AZoneActor()
{
    bReplicates                   = true;
    bAlwaysRelevant               = true; // All clients need zone state regardless of distance
    PrimaryActorTick.bCanEverTick = false;

    BoxShape    = CreateDefaultSubobject<UZoneBoxShapeComponent>(TEXT("BoxShape"));
    ConvexShape = CreateDefaultSubobject<UZoneConvexPolygonShapeComponent>(TEXT("ConvexShape"));
    SetRootComponent(BoxShape);
}

void AZoneActor::BeginPlay()
{
    Super::BeginPlay();
    if (UZoneSubsystem* Sub = GetWorld()->GetSubsystem<UZoneSubsystem>())
        Sub->RegisterZone(this);
}

void AZoneActor::EndPlay(EEndPlayReason::Type Reason)
{
    if (UZoneSubsystem* Sub = GetWorld()->GetSubsystem<UZoneSubsystem>())
        Sub->UnregisterZone(this);
    Super::EndPlay(Reason);
}

void AZoneActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutProps) const
{
    Super::GetLifetimeReplicatedProps(OutProps);
    DOREPLIFETIME(AZoneActor, DynamicState);
}

// =============================================================================
// Query API
// =============================================================================

UZoneShapeComponent* AZoneActor::GetActiveShape() const
{
    return ShapeType == EZoneShapeType::Box
        ? static_cast<UZoneShapeComponent*>(BoxShape)
        : static_cast<UZoneShapeComponent*>(ConvexShape);
}

bool AZoneActor::ContainsPoint(const FVector& WorldPoint) const
{
    if (const UZoneShapeComponent* Shape = GetActiveShape())
        return Shape->ContainsPoint(WorldPoint);
    return false;
}

FBox AZoneActor::GetWorldBounds() const
{
    if (const UZoneShapeComponent* Shape = GetActiveShape())
        return Shape->GetWorldBounds();
    return FBox(EForceInit::ForceInit);
}

// =============================================================================
// Mutation API
// =============================================================================

void AZoneActor::SetOwnerTag(FGameplayTag NewOwner)
{
    DynamicState.OwnerTag = NewOwner;
    BroadcastStateChanged(EGameCoreEventScope::ServerOnly);
}

void AZoneActor::AddDynamicTag(FGameplayTag Tag)
{
    DynamicState.DynamicTags.AddTag(Tag);
    BroadcastStateChanged(EGameCoreEventScope::ServerOnly);
}

void AZoneActor::RemoveDynamicTag(FGameplayTag Tag)
{
    DynamicState.DynamicTags.RemoveTag(Tag);
    BroadcastStateChanged(EGameCoreEventScope::ServerOnly);
}

void AZoneActor::OnRep_DynamicState()
{
    BroadcastStateChanged(EGameCoreEventScope::ClientOnly);
}

void AZoneActor::BroadcastStateChanged(EGameCoreEventScope Scope)
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FZoneStateChangedMessage Msg;
        Msg.ZoneActor    = this;
        Msg.StaticData   = DataAsset;
        Msg.DynamicState = DynamicState;
        Bus->Broadcast(GameCore::Zone::Tags::Channel_StateChanged,
            FInstancedStruct::Make(Msg), Scope);
    }
}

// =============================================================================
// InitializeZone
// =============================================================================

void AZoneActor::InitializeZone(
    UZoneDataAsset* InDataAsset, const FZoneShapeData& InShapeData)
{
    DataAsset = InDataAsset;
    ShapeType = InShapeData.ShapeType;

    if (ShapeType == EZoneShapeType::Box)
    {
        BoxShape->HalfExtent = InShapeData.BoxExtent;
        BoxShape->RebuildCache();
    }
    else
    {
        ConvexShape->LocalPolygonPoints = InShapeData.PolygonPoints;
        ConvexShape->MinZ = InShapeData.MinZ;
        ConvexShape->MaxZ = InShapeData.MaxZ;
        ConvexShape->RebuildWorldPolygon();
    }
}
