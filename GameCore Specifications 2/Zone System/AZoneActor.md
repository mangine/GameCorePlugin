# AZoneActor

**File:** `GameCore/Source/GameCore/Zone/ZoneActor.h` / `.cpp`

The replicated world actor that owns one active shape component and the mutable `FZoneDynamicState`. This is the entity clients and server both know about.

---

## Class Definition

```cpp
UCLASS(BlueprintType)
class GAMECORE_API AZoneActor : public AActor
{
    GENERATED_BODY()

public:
    AZoneActor();

    // ---- Static Data ----

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    TObjectPtr<UZoneDataAsset> DataAsset;

    // ---- Shape ----
    // Both components are always created; only the active one is queried.
    // ShapeType selects which component GetActiveShape() returns.

    UPROPERTY(EditAnywhere, Category="Zone")
    EZoneShapeType ShapeType = EZoneShapeType::Box;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
    TObjectPtr<UZoneBoxShapeComponent> BoxShape;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
    TObjectPtr<UZoneConvexPolygonShapeComponent> ConvexShape;

    // ---- Mutable State (replicated) ----

    UPROPERTY(ReplicatedUsing=OnRep_DynamicState, BlueprintReadOnly, Category="Zone")
    FZoneDynamicState DynamicState;

    // ---- Query API ----

    // Returns true if WorldPoint is inside this zone.
    UFUNCTION(BlueprintCallable, Category="Zone")
    bool ContainsPoint(const FVector& WorldPoint) const;

    // World-space AABB for spatial index registration.
    FBox GetWorldBounds() const;

    // ---- Mutation API (server only) ----

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Zone")
    void SetOwnerTag(FGameplayTag NewOwner);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Zone")
    void AddDynamicTag(FGameplayTag Tag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Zone")
    void RemoveDynamicTag(FGameplayTag Tag);

    // Server-only: configure zone after runtime spawn.
    // Must be called before the actor is registered with UZoneSubsystem (i.e. before BeginPlay,
    // or immediately after spawn if BeginPlay already ran).
    void InitializeZone(UZoneDataAsset* InDataAsset, const FZoneShapeData& InShapeData);

    // ---- UE Overrides ----
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutProps) const override;

private:
    UFUNCTION()
    void OnRep_DynamicState();

    UZoneShapeComponent* GetActiveShape() const;

    void BroadcastStateChanged();
};
```

---

## Constructor

```cpp
AZoneActor::AZoneActor()
{
    bReplicates        = true;
    bAlwaysRelevant    = true;  // All clients need zone state regardless of distance
    PrimaryActorTick.bCanEverTick = false;

    BoxShape    = CreateDefaultSubobject<UZoneBoxShapeComponent>(TEXT("BoxShape"));
    ConvexShape = CreateDefaultSubobject<UZoneConvexPolygonShapeComponent>(TEXT("ConvexShape"));
    SetRootComponent(BoxShape);
}
```

---

## Key Implementations

```cpp
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
```

---

## Mutation & Broadcasting

Mutation methods are `BlueprintAuthorityOnly`. After mutating, they broadcast `FZoneStateChangedMessage` immediately on the server via `BroadcastStateChanged()`. The property replication sends `FZoneDynamicState` to clients, where `OnRep_DynamicState` fires the same message on the client side.

```cpp
void AZoneActor::BroadcastStateChanged()
{
    // Server broadcasts with ServerOnly scope.
    // Clients receive the same message via OnRep_DynamicState.
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FZoneStateChangedMessage Msg;
        Msg.ZoneActor    = this;
        Msg.StaticData   = DataAsset;
        Msg.DynamicState = DynamicState;
        Bus->Broadcast<FZoneStateChangedMessage>(
            GameCore::Zone::Tags::Channel_StateChanged,
            Msg,
            EGameCoreEventScope::ServerOnly);
    }
}

void AZoneActor::SetOwnerTag(FGameplayTag NewOwner)
{
    DynamicState.OwnerTag = NewOwner;
    BroadcastStateChanged();
}

void AZoneActor::AddDynamicTag(FGameplayTag Tag)
{
    DynamicState.DynamicTags.AddTag(Tag);
    BroadcastStateChanged();
}

void AZoneActor::RemoveDynamicTag(FGameplayTag Tag)
{
    DynamicState.DynamicTags.RemoveTag(Tag);
    BroadcastStateChanged();
}

void AZoneActor::OnRep_DynamicState()
{
    // Fires on clients only. Broadcasts with ClientOnly scope.
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FZoneStateChangedMessage Msg;
        Msg.ZoneActor    = this;
        Msg.StaticData   = DataAsset;
        Msg.DynamicState = DynamicState;
        Bus->Broadcast<FZoneStateChangedMessage>(
            GameCore::Zone::Tags::Channel_StateChanged,
            Msg,
            EGameCoreEventScope::ClientOnly);
    }
}
```

---

## `InitializeZone`

Called after runtime spawn to configure shape from data. Must be called before registration with `UZoneSubsystem` (i.e. before or in the same frame as `BeginPlay`).

```cpp
void AZoneActor::InitializeZone(UZoneDataAsset* InDataAsset, const FZoneShapeData& InShapeData)
{
    DataAsset = InDataAsset;
    ShapeType = InShapeData.ShapeType;

    if (ShapeType == EZoneShapeType::Box)
    {
        BoxShape->HalfExtent = InShapeData.BoxExtent;
        BoxShape->RebuildCache(); // RebuildCache is a public method on UZoneBoxShapeComponent
    }
    else
    {
        ConvexShape->LocalPolygonPoints = InShapeData.PolygonPoints;
        ConvexShape->MinZ = InShapeData.MinZ;
        ConvexShape->MaxZ = InShapeData.MaxZ;
        ConvexShape->RebuildWorldPolygon();
    }
}
```

> **Note:** `bAlwaysRelevant = true` is intentional. Zones must be known to all clients regardless of position so freshly connected players have correct state. At 50–500 zones this is acceptable. Revisit with interest management if zone count grows beyond this range.
