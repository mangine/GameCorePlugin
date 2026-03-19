# Extension Patterns & Usage Samples

Practical recipes for the most common Zone System use cases. All examples assume the game module has its own `UPirateZoneDataAsset` subclass and a tag taxonomy defined in `PirateZoneTags`.

---

## 1. Extending Zone Data

Subclass `UZoneDataAsset` in the game module to add domain-specific fields. The Zone System never touches these fields — they are entirely yours.

```cpp
// PirateZoneDataAsset.h  (game module)
UCLASS(BlueprintType)
class UPirateZoneDataAsset : public UZoneDataAsset
{
    GENERATED_BODY()

public:
    // Tax rate applied to trades conducted inside this territory (0.0 - 1.0)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Territory")
    float TaxRate = 0.05f;

    // Faction that authored this zone (default controller at spawn)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Territory")
    FGameplayTag OwningFaction;

    // Danger level for AI aggression scaling
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Danger")
    int32 DangerLevel = 1;

    // Ambient audio asset to play when inside this zone
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmetics")
    TSoftObjectPtr<USoundBase> AmbientSound;
};
```

In the editor, assign a `UPirateZoneDataAsset` to any `AZoneActor`. The base pointer stored on the actor is `UZoneDataAsset*` — cast wherever you need pirate-specific fields.

---

## 2. Extending Dynamic State

If your mutable data goes beyond owner tag and dynamic gameplay tags, extend via a custom replicated component added to `AZoneActor` subclass. Keep `FZoneDynamicState` lean — it replicates to all clients.

```cpp
// PirateZoneActor.h  (game module)
UCLASS()
class APirateZoneActor : public AZoneActor
{
    GENERATED_BODY()

    // Extra replicated state specific to pirate territory
    UPROPERTY(ReplicatedUsing=OnRep_TerritoryState)
    FPirateTerritoryState TerritoryState;  // troop count, siege status, etc.

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutProps) const override;
};
```

---

## 3. Reading Zone Data from UI

A territory panel widget that shows tax rate and current owner for any zone — no transition required.

```cpp
void UTerritoryInfoWidget::PopulateForZone(FGameplayTag ZoneNameTag)
{
    UZoneSubsystem* ZoneSys = GetWorld()->GetSubsystem<UZoneSubsystem>();
    if (!ZoneSys) return;

    AZoneActor* Zone = ZoneSys->GetZoneByName(ZoneNameTag);
    if (!Zone) return;

    // Cast to game-specific asset for extended fields
    const UPirateZoneDataAsset* Data = Cast<UPirateZoneDataAsset>(Zone->DataAsset);
    if (!Data) return;

    ZoneNameText->SetText(Data->DisplayName);
    TaxRateText->SetText(FText::AsPercent(Data->TaxRate));
    DangerLevelText->SetText(FText::AsNumber(Data->DangerLevel));

    // Dynamic state (owner) is on the actor, replicated from server
    OwnerTagText->SetText(FText::FromName(Zone->DynamicState.OwnerTag.GetTagName()));
}
```

---

## 4. Listing All Zones of a Given Type

Filter zones by type tag to build a territory map, a weather overview, or a world event list.

```cpp
// Get all territory zones
TArray<AZoneActor*> Territories = ZoneSys->GetZonesByType(PirateZoneTags::ZoneType_Territory);

// Populate a map widget entry for each
for (AZoneActor* Zone : Territories)
{
    if (const UPirateZoneDataAsset* Data = Cast<UPirateZoneDataAsset>(Zone->DataAsset))
    {
        FTerritoryMapEntry Entry;
        Entry.ZoneActor   = Zone;
        Entry.DisplayName = Data->DisplayName;
        Entry.OwnerTag    = Zone->DynamicState.OwnerTag;
        Entry.TaxRate     = Data->TaxRate;
        MapWidget->AddEntry(Entry);
    }
}
```

---

## 5. Filtering by Dynamic Tags

Dynamic tags on `FZoneDynamicState` allow runtime state filtering without subclassing.

```cpp
// Find all territories currently under siege
TArray<AZoneActor*> Territories = ZoneSys->GetZonesByType(PirateZoneTags::ZoneType_Territory);

TArray<AZoneActor*> UnderSiege = Territories.FilterByPredicate([](const AZoneActor* Z)
{
    return Z->DynamicState.DynamicTags.HasTag(PirateZoneTags::State_UnderSiege);
});
```

The server sets siege state:

```cpp
// Server-side event system
Zone->AddDynamicTag(PirateZoneTags::State_UnderSiege);
// All clients automatically receive the tag via replication + OnRep_DynamicState GMS broadcast
```

---

## 6. Reacting to Zone Transitions (Player enters territory)

Listen for `FZoneTransitionMessage` via GMS. Filter by zone type inside the handler.

```cpp
// UPirateTerritorySystem::BeginPlay
UGameplayMessageSubsystem& GMS = UGameplayMessageSubsystem::Get(this);
TransitionHandle = GMS.RegisterListener<FZoneTransitionMessage>(
    GameCore::Zone::Tags::Channel_Transition,
    this,
    &UPirateTerritorySystem::OnZoneTransition
);

void UPirateTerritorySystem::OnZoneTransition(
    FGameplayTag Channel, const FZoneTransitionMessage& Msg)
{
    if (!Msg.StaticData) return;
    if (!Msg.StaticData->ZoneTypeTag.MatchesTag(PirateZoneTags::ZoneType_Territory)) return;

    const UPirateZoneDataAsset* Data = Cast<UPirateZoneDataAsset>(Msg.StaticData);
    if (!Data) return;

    if (Msg.bEntered)
    {
        NotifyPlayerEnteredTerritory(Msg.TrackedActor, Data, Msg.DynamicState.OwnerTag);
        ApplyTaxModifier(Msg.TrackedActor, Data->TaxRate);
    }
    else
    {
        RemoveTaxModifier(Msg.TrackedActor);
    }
}

// UPirateTerritorySystem::EndPlay
UGameplayMessageSubsystem::Get(this).UnregisterListener(TransitionHandle);
```

---

## 7. Reacting to Ownership Changes (Territory capture)

Listen on `Channel_StateChanged` to update HUD, minimap, or AI patrol targets when a zone changes hands.

```cpp
StateHandle = GMS.RegisterListener<FZoneStateChangedMessage>(
    GameCore::Zone::Tags::Channel_StateChanged,
    this,
    &UMinimapSystem::OnZoneStateChanged
);

void UMinimapSystem::OnZoneStateChanged(
    FGameplayTag Channel, const FZoneStateChangedMessage& Msg)
{
    if (!Msg.StaticData) return;
    if (!Msg.StaticData->ZoneTypeTag.MatchesTag(PirateZoneTags::ZoneType_Territory)) return;

    // Refresh minimap colour for this zone based on new owner
    RefreshZoneColour(Msg.ZoneActor, Msg.DynamicState.OwnerTag);
}
```

---

## 8. Cosmetic-Only (Client-Side) Zone Response

The tracker component fires on the client too. Local cosmetic systems subscribe and act immediately without waiting for server confirmation. This is intentional — cosmetics don't need authority.

```cpp
// UAmbientAudioSystem (client-side UActorComponent on the local player)
void UAmbientAudioSystem::OnZoneTransition(
    FGameplayTag Channel, const FZoneTransitionMessage& Msg)
{
    // Only care about local player
    if (Msg.TrackedActor != GetOwner()) return;

    const UPirateZoneDataAsset* Data = Cast<UPirateZoneDataAsset>(Msg.StaticData);
    if (!Data) return;

    if (Msg.bEntered && !Data->AmbientSound.IsNull())
        PlayAmbientSound(Data->AmbientSound);
    else if (!Msg.bEntered)
        StopAmbientSound();
}
```

> **Multi-zone stacking with cosmetics:** If the player is simultaneously in a Weather zone and a Territory zone, both enter events fire independently. Your audio system should stack or prioritise based on `DataAsset->Priority` — the tracker doesn't enforce cosmetic precedence, that's the consumer's responsibility.

---

## 9. Direct Point Query (no tracker)

For systems that need to check zone membership on-demand without owning an actor (e.g. a server-side trade validation):

```cpp
bool UTradeSystem::IsTaxApplicable(const FVector& TradeLocation, float& OutTaxRate)
{
    UZoneSubsystem* ZoneSys = GetWorld()->GetSubsystem<UZoneSubsystem>();

    // GetTopZone returns highest-priority zone at the point
    AZoneActor* Zone = ZoneSys->QueryTopZoneAtPoint(TradeLocation);
    if (!Zone) return false;

    const UPirateZoneDataAsset* Data = Cast<UPirateZoneDataAsset>(Zone->DataAsset);
    if (!Data) return false;

    OutTaxRate = Data->TaxRate;
    return true;
}
```

---

## 10. Runtime Zone Spawn (Event Zone)

Server spawns a temporary danger zone for a world event. Zone is static after spawn.

```cpp
// Server only — called by an event system
void UWorldEventSystem::SpawnEventZone(
    FVector Center, float Radius, UPirateZoneDataAsset* EventDataAsset)
{
    FTransform SpawnTransform(FRotator::ZeroRotator, Center);
    AZoneActor* Zone = GetWorld()->SpawnActor<AZoneActor>(
        AZoneActor::StaticClass(), SpawnTransform);

    FZoneShapeData Shape;
    Shape.ShapeType  = EZoneShapeType::Box;
    Shape.BoxExtent  = FVector(Radius, Radius, 500.f);

    Zone->InitializeZone(EventDataAsset, Shape);
    // AZoneActor::BeginPlay auto-registers with UZoneSubsystem
    // bReplicates=true means clients receive the actor automatically

    // Store reference to despawn later
    ActiveEventZones.Add(Zone);
}

void UWorldEventSystem::DespawnEventZone(AZoneActor* Zone)
{
    // UZoneSubsystem::UnregisterZone called automatically via EndPlay
    Zone->Destroy();
    ActiveEventZones.Remove(Zone);
}
```
