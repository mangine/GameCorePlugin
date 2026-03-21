# Zone System — Usage

## 1. Author a Zone in the Editor

1. Create a `UZoneDataAsset` asset. Set `ZoneNameTag` (e.g. `Zone.BlackpearlBay`), `ZoneTypeTag` (e.g. `Zone.Type.Territory`), and any `StaticGameplayTags`.
2. Place an `AZoneActor` in the level. Assign the Data Asset.
3. Set `ShapeType` to `Box` or `ConvexPolygon` and configure the active shape component in the Details panel.

---

## 2. Make an Actor Zone-Aware

Add `UZoneTrackerComponent` to any actor in its constructor:

```cpp
// In MyPawn.h
UPROPERTY(VisibleAnywhere)
TObjectPtr<UZoneTrackerComponent> ZoneTracker;

// In MyPawn.cpp constructor
ZoneTracker = CreateDefaultSubobject<UZoneTrackerComponent>(TEXT("ZoneTracker"));
```

For large actors such as ships, set a specific anchor point:

```cpp
// After CreateDefaultSubobject, in constructor or BeginPlay
ZoneTracker->LocationAnchor = HullAnchorComponent; // USceneComponent* on the ship
ZoneTracker->QueryInterval  = 0.3f;                // Less frequent for large slow actors
```

The component handles everything automatically from `BeginPlay`.

---

## 3. Listen for Zone Transitions

```cpp
// MySystem.h
private:
    FGameplayMessageListenerHandle TransitionHandle;

// MySystem.cpp
void UMySystem::BeginPlay()
{
    Super::BeginPlay();

    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        TransitionHandle = Bus->StartListening<FZoneTransitionMessage>(
            GameCore::Zone::Tags::Channel_Transition,
            [this](FGameplayTag, const FZoneTransitionMessage& Msg)
            {
                if (!Msg.StaticData) return;

                if (Msg.bEntered)
                    UE_LOG(LogTemp, Log, TEXT("%s entered zone %s"),
                        *GetNameSafe(Msg.TrackedActor),
                        *Msg.StaticData->ZoneNameTag.ToString());
                else
                    UE_LOG(LogTemp, Log, TEXT("%s exited zone %s"),
                        *GetNameSafe(Msg.TrackedActor),
                        *Msg.StaticData->ZoneNameTag.ToString());
            });
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        Bus->StopListening(TransitionHandle);
    Super::EndPlay(Reason);
}
```

---

## 4. Listen for Zone State Changes (Ownership / Dynamic Tags)

```cpp
// MyTerritoryHUD.h
private:
    FGameplayMessageListenerHandle StateHandle;

// MyTerritoryHUD.cpp
void UMyTerritoryHUD::BeginPlay()
{
    Super::BeginPlay();

    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        StateHandle = Bus->StartListening<FZoneStateChangedMessage>(
            GameCore::Zone::Tags::Channel_StateChanged,
            [this](FGameplayTag, const FZoneStateChangedMessage& Msg)
            {
                if (!Msg.StaticData) return;
                RefreshTerritoryUI(Msg.ZoneActor, Msg.DynamicState.OwnerTag);
            });
    }
}

void UMyTerritoryHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        Bus->StopListening(StateHandle);
    Super::EndPlay(Reason);
}
```

---

## 5. Point Query (Without a Tracker)

For on-demand queries from systems that don't own an actor:

```cpp
UZoneSubsystem* ZoneSys = GetWorld()->GetSubsystem<UZoneSubsystem>();
if (!ZoneSys) return;

// All zones containing this point, sorted by priority
TArray<AZoneActor*> Zones = ZoneSys->QueryZonesAtPoint(SomeLocation);

// Highest-priority zone only
AZoneActor* TopZone = ZoneSys->QueryTopZoneAtPoint(SomeLocation);

// All zones of a given type (no spatial filter)
TArray<AZoneActor*> Territories = ZoneSys->GetZonesByType(MyZoneTags::Type_Territory);

// Direct lookup by name
AZoneActor* Bay = ZoneSys->GetZoneByName(MyZoneTags::Name_BlackpearlBay);
```

---

## 6. Mutate Zone State (Server Only)

```cpp
// Only valid on the server (BlueprintAuthorityOnly)
Zone->SetOwnerTag(FactionTags::Faction_BritishEmpire);
Zone->AddDynamicTag(ZoneStateTags::State_UnderSiege);
Zone->RemoveDynamicTag(ZoneStateTags::State_UnderSiege);
```

Mutations replicate to clients automatically. Both server and client fire `FZoneStateChangedMessage` on the Event Bus (server immediately, clients via `OnRep_DynamicState`).

---

## 7. Spawn a Runtime Zone (Event Zone)

```cpp
// Server only
FTransform SpawnTransform(FRotator::ZeroRotator, Center);
AZoneActor* Zone = GetWorld()->SpawnActor<AZoneActor>(AZoneActor::StaticClass(), SpawnTransform);

FZoneShapeData Shape;
Shape.ShapeType = EZoneShapeType::Box;
Shape.BoxExtent = FVector(Radius, Radius, 500.f);

Zone->InitializeZone(EventDataAsset, Shape);
// Auto-registers with UZoneSubsystem in BeginPlay
// bReplicates=true — clients receive the actor automatically

// Later: destroy it (auto-unregisters via EndPlay)
Zone->Destroy();
```

---

## 8. Extend Zone Data (Game Module)

Subclass `UZoneDataAsset` in the game module to add domain-specific fields:

```cpp
// PirateZoneDataAsset.h  (game module)
UCLASS(BlueprintType)
class UPirateZoneDataAsset : public UZoneDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Territory")
    float TaxRate = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Territory")
    FGameplayTag DefaultOwningFaction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics")
    TSoftObjectPtr<USoundBase> AmbientSound;
};
```

Assign a `UPirateZoneDataAsset` to any `AZoneActor`. Cast at the listener:

```cpp
const UPirateZoneDataAsset* Data = Cast<UPirateZoneDataAsset>(Msg.StaticData);
if (Data) { /* use Data->TaxRate etc. */ }
```

---

## 9. Filter by Dynamic Tags

```cpp
// Find all territories currently under siege
TArray<AZoneActor*> AllTerritories = ZoneSys->GetZonesByType(PirateZoneTags::Type_Territory);

TArray<AZoneActor*> UnderSiege = AllTerritories.FilterByPredicate([](const AZoneActor* Z)
{
    return Z->DynamicState.DynamicTags.HasTag(PirateZoneTags::State_UnderSiege);
});
```

---

## 10. Check Current Zones on the Tracker

```cpp
// From any system with a reference to the actor's tracker
const TArray<AZoneActor*>& Current = ZoneTracker->GetCurrentZones();

// Convenience check
if (ZoneTracker->IsInZoneOfType(PirateZoneTags::Type_SafeHarbour))
{
    // Inside a safe harbour
}
```
