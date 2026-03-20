# Integration Guide

**Sub-page of:** [Inventory System](../Inventory%20System.md)

---

## What the Inventory System Does and Does Not Do

**Does:**
- Manage item slots, stacking, partial pickup, dropping, and dismantling
- Enforce weight and slot count constraints (two-tier soft/hard caps)
- Validate per-slot requirements via `UTaggedSlotLayout`
- Replicate slot state to the owning client via `FFastArraySerializer`
- Persist inventory state via `IPersistableComponent`
- Emit GMS events for all mutations
- Audit all mutations via `FGameCoreBackend`

**Does not:**
- Define item data assets or item types
- Know item weight, stack size, or display name (bridges via `IItemDefinitionProvider`)
- Spawn world pickup actors on drop
- Grant dismantle rewards
- Enforce anti-cheat rate limits (offline audit analysis handles this)
- Know about GAS (weight cap changes are bridged by the game module)

---

## Required Setup

### 1. Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "GameplayTags",
    "NetCore",
});
```

### 2. Add UInventoryComponent to your Actor

Typically on `APlayerState` or `APawn`. Persistence wiring requires a `UPersistenceRegistrationComponent` on the same actor.

```cpp
// In AMyPlayerState constructor
InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
```

Configure in the Blueprint default or C++ constructor:

```cpp
// Open bag with weight + slot count constraints
InventoryComponent->WeightConstraint =
    CreateDefaultSubobject<UWeightConstraint>(TEXT("WeightConstraint"));
InventoryComponent->WeightConstraint->WeightLimit = 500.f; // Hard cap
InventoryComponent->WeightConstraint->MaxWeight   = 0.f;   // Set at BeginPlay via GAS

InventoryComponent->SlotCountConstraint =
    CreateDefaultSubobject<USlotCountConstraint>(TEXT("SlotConstraint"));
InventoryComponent->SlotCountConstraint->SlotLimit = 60;
InventoryComponent->SlotCountConstraint->MaxSlots  = 20; // Expandable via bags

// Optional: set a tagged layout for equipment slots
InventoryComponent->SlotLayout =
    CreateDefaultSubobject<UTaggedSlotLayout>(TEXT("SlotLayout"));
```

### 3. Wire IItemDefinitionProvider at BeginPlay

The game module implements `IItemDefinitionProvider` on a subsystem or singleton.

```cpp
// In AMyPlayerState::BeginPlay (server only)
void AMyPlayerState::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        UMyItemRegistry* Registry =
            GetWorld()->GetSubsystem<UMyItemRegistry>();
        InventoryComponent->SetItemDefinitionProvider(Registry);
    }
}
```

### 4. Wire GAS MaxCarryWeight Attribute

```cpp
// In AMyPlayerState::BeginPlay, after ASC is initialised
if (HasAuthority() && InventoryComponent->WeightConstraint)
{
    UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    ASC->GetGameplayAttributeValueChangeDelegate(
            UMyAttributeSet::GetMaxCarryWeightAttribute())
        .AddLambda([this](const FOnAttributeChangeData& Data)
        {
            if (InventoryComponent->WeightConstraint)
                InventoryComponent->WeightConstraint
                    ->NotifyMaxWeightChanged(Data.NewValue);
        });

    // Initialise with current value.
    InventoryComponent->WeightConstraint->NotifyMaxWeightChanged(
        ASC->GetNumericAttribute(
            UMyAttributeSet::GetMaxCarryWeightAttribute()));
}
```

### 5. Handle Drop Event (spawn world actor)

```cpp
// In AMyGameMode or a dedicated pickup manager — game module
void AMyGameMode::BeginPlay()
{
    Super::BeginPlay();
    UGameCoreEventSubsystem* Bus =
        GetWorld()->GetSubsystem<UGameCoreEventSubsystem>();
    Bus->Subscribe<FInventoryItemDroppedMessage>(
        TAG_GameCoreEvent_Inventory_ItemDropped, this,
        &AMyGameMode::OnItemDropped);
}

void AMyGameMode::OnItemDropped(
    const FGameplayTag&, const FInventoryItemDroppedMessage& Msg)
{
    FActorSpawnParameters Params;
    AMyPickupActor* Pickup = GetWorld()->SpawnActor<AMyPickupActor>(
        AMyPickupActor::StaticClass(), Msg.DropLocation,
        FRotator::ZeroRotator, Params);
    Pickup->Initialize(Msg.DroppedSlot);
}
```

### 6. Handle Dismantle Event (grant resources)

```cpp
Bus->Subscribe<FInventoryItemDismantledMessage>(
    TAG_GameCoreEvent_Inventory_ItemDismantled, this,
    &AMyGameMode::OnItemDismantled);

void AMyGameMode::OnItemDismantled(
    const FGameplayTag&, const FInventoryItemDismantledMessage& Msg)
{
    // Decode InstanceData using game module schema.
    FMyItemInstanceData InstanceData;
    FMemoryReader Reader(Msg.DismantledSlot.InstanceData);
    Reader << InstanceData;

    // Grant materials based on item type and instance state.
    UMyCraftingSubsystem* Crafting =
        GetWorld()->GetSubsystem<UMyCraftingSubsystem>();
    Crafting->GrantDismantleRewards(
        Cast<APlayerState>(Msg.OwnerActor),
        Msg.DismantledSlot.ItemTag,
        InstanceData);
}
```

---

## Common Usage Patterns

### Server-side pickup (client RPC entry point)

```cpp
void UMyInventoryBridge::ServerRPC_Pickup_Implementation(
    AActor* SourceActor)
{
    // Validate source.
    AMyPickupActor* Pickup = Cast<AMyPickupActor>(SourceActor);
    if (!Pickup || !Pickup->IsAvailable()) return;

    FRequirementContext Context;
    Context.PlayerState = GetOwner<APlayerState>();

    FInventoryAutoPlaceResult Result = InventoryComponent->TryPlaceAuto(
        Pickup->ItemTag, Pickup->Quantity, Context);

    if (Result.QuantityPlaced > 0)
    {
        // Authority place — no re-check needed.
        InventoryComponent->PlaceAuto(
            Pickup->ItemTag, Result.QuantityPlaced, Pickup->InstanceData);
        Pickup->ConsumeQuantity(Result.QuantityPlaced);
    }

    if (Result.QuantityRemaining > 0)
    {
        // Notify client: partial pickup, X items remain on the ground.
        ClientRPC_PickupPartial(Result.QuantityRemaining);
    }
}
```

### Tagged slot placement (equipment)

```cpp
// Try to equip a helmet to slot index 0.
FRequirementContext Context;
Context.PlayerState = PlayerState;

EInventoryMutationResult Result = InventoryComponent->TryPlaceAt(
    0, TAG_Item_Helmet_IronHelmet, 1, Context);

if (Result == EInventoryMutationResult::Success)
    InventoryComponent->PlaceAt(0, TAG_Item_Helmet_IronHelmet, 1);
else
    // Inform UI: why placement failed.
    HandlePlacementFailure(Result);
```

### Expand slots when a bag is equipped

```cpp
// When the player equips a bag item:
void UMyEquipmentBridge::OnBagEquipped(FGameplayTag BagTag)
{
    UMyItemRegistry* Registry =
        GetWorld()->GetSubsystem<UMyItemRegistry>();
    const int32 BagSlots = Registry->GetBagSlotBonus(BagTag);

    if (USlotCountConstraint* C = InventoryComponent->SlotCountConstraint)
        C->NotifyMaxSlotsChanged(C->GetMaxSlots() + BagSlots);
}
```

---

## Gameplay Tags Setup

Copy the following into your `DefaultGameplayTags.ini`:

```ini
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemAdded")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemRemoved")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemDropped")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemDismantled")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.WeightChanged")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ConstraintViolated")
+GameplayTagList=(Tag="RequirementEvent.Inventory.ItemAdded")
+GameplayTagList=(Tag="RequirementEvent.Inventory.ItemRemoved")
+GameplayTagList=(Tag="Audit.Inventory.Add")
+GameplayTagList=(Tag="Audit.Inventory.Remove")
+GameplayTagList=(Tag="Audit.Inventory.Drop")
+GameplayTagList=(Tag="Audit.Inventory.Dismantle")
```
