# Inventory System — Usage

**Part of:** GameCore Plugin | **UE Version:** 5.7

This document covers how to set up, configure, and interact with the Inventory System from the game module.

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

## Build.cs Setup

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "GameplayTags",
    "NetCore",
});
```

---

## Step 1 — Add UInventoryComponent to Your Actor

Typically on `APlayerState` or `APawn`. Persistence requires a `UPersistenceRegistrationComponent` on the same actor.

```cpp
// AMyPlayerState.h
UPROPERTY()
TObjectPtr<UInventoryComponent> InventoryComponent;

// AMyPlayerState.cpp — constructor
InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));

// Configure constraints and layout in the Blueprint default or C++ constructor:
InventoryComponent->WeightConstraint =
    CreateDefaultSubobject<UWeightConstraint>(TEXT("WeightConstraint"));
InventoryComponent->WeightConstraint->WeightLimit = 500.f; // Hard cap
// MaxWeight starts at 0; set at BeginPlay from GAS attribute

InventoryComponent->SlotCountConstraint =
    CreateDefaultSubobject<USlotCountConstraint>(TEXT("SlotConstraint"));
InventoryComponent->SlotCountConstraint->SlotLimit = 60;
InventoryComponent->SlotCountConstraint->MaxSlots  = 20; // Expandable via bags
```

---

## Step 2 — Implement IItemDefinitionProvider

The game module must implement `IItemDefinitionProvider` on a subsystem or singleton and supply it to the component at `BeginPlay`.

```cpp
// UMyItemRegistry.h (game module)
UCLASS()
class UMyItemRegistry : public UWorldSubsystem, public IItemDefinitionProvider
{
    GENERATED_BODY()
public:
    virtual float GetItemWeight(FGameplayTag ItemTag) const override;
    virtual int32 GetMaxStackSize(FGameplayTag ItemTag) const override;
    virtual bool  IsValidItem(FGameplayTag ItemTag)   const override;
};

// AMyPlayerState::BeginPlay (server only)
void AMyPlayerState::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        UMyItemRegistry* Registry = GetWorld()->GetSubsystem<UMyItemRegistry>();
        InventoryComponent->SetItemDefinitionProvider(Registry);
    }
}
```

---

## Step 3 — Wire GAS MaxCarryWeight Attribute

```cpp
// AMyPlayerState::BeginPlay, after ASC is initialised
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

    // Initialise with the current value.
    InventoryComponent->WeightConstraint->NotifyMaxWeightChanged(
        ASC->GetNumericAttribute(UMyAttributeSet::GetMaxCarryWeightAttribute()));
}
```

---

## Step 4 — Handle Drop Event (spawn world pickup)

GameCore only broadcasts the event. The game module is responsible for spawning the world actor.

```cpp
// AMyGameMode::BeginPlay
UGameCoreEventSubsystem* Bus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>();
Bus->Subscribe<FInventoryItemDroppedMessage>(
    TAG_GameCoreEvent_Inventory_ItemDropped, this,
    &AMyGameMode::OnItemDropped);

void AMyGameMode::OnItemDropped(
    const FGameplayTag&, const FInventoryItemDroppedMessage& Msg)
{
    AMyPickupActor* Pickup = GetWorld()->SpawnActor<AMyPickupActor>(
        AMyPickupActor::StaticClass(), Msg.DropLocation,
        FRotator::ZeroRotator);
    if (Pickup)
        Pickup->Initialize(Msg.DroppedSlot);
}
```

---

## Step 5 — Handle Dismantle Event (grant resources)

```cpp
Bus->Subscribe<FInventoryItemDismantledMessage>(
    TAG_GameCoreEvent_Inventory_ItemDismantled, this,
    &AMyGameMode::OnItemDismantled);

void AMyGameMode::OnItemDismantled(
    const FGameplayTag&, const FInventoryItemDismantledMessage& Msg)
{
    FMyItemInstanceData InstanceData;
    FMemoryReader Reader(Msg.DismantledSlot.InstanceData);
    Reader << InstanceData;

    UMyCraftingSubsystem* Crafting = GetWorld()->GetSubsystem<UMyCraftingSubsystem>();
    Crafting->GrantDismantleRewards(
        Cast<APlayerState>(Msg.OwnerActor),
        Msg.DismantledSlot.ItemTag,
        InstanceData);
}
```

---

## Common Usage Patterns

### Server-side pickup RPC

```cpp
void UMyInventoryBridge::ServerRPC_Pickup_Implementation(AActor* SourceActor)
{
    AMyPickupActor* Pickup = Cast<AMyPickupActor>(SourceActor);
    if (!Pickup || !Pickup->IsAvailable()) return;

    FRequirementContext Context;
    Context.PlayerState = GetOwner<APlayerState>();

    // Validate first (respects soft caps, client-predicted).
    FInventoryAutoPlaceResult TryResult = InventoryComponent->TryPlaceAuto(
        Pickup->ItemTag, Pickup->Quantity, Context);

    if (TryResult.QuantityPlaced > 0)
    {
        // Authority place — no re-check.
        InventoryComponent->PlaceAuto(
            Pickup->ItemTag, TryResult.QuantityPlaced, Pickup->InstanceData);
        Pickup->ConsumeQuantity(TryResult.QuantityPlaced);
    }

    if (TryResult.QuantityRemaining > 0)
        ClientRPC_PickupPartial(TryResult.QuantityRemaining);
}
```

### Tagged slot placement (equipment)

```cpp
// Try to equip a helmet to slot index 0 (server-side, after RPC validation).
FRequirementContext Context;
Context.PlayerState = PlayerState;

EInventoryMutationResult Result = InventoryComponent->TryPlaceAt(
    0, TAG_Item_Helmet_IronHelmet, 1, Context);

if (Result == EInventoryMutationResult::Success)
    InventoryComponent->PlaceAt(0, TAG_Item_Helmet_IronHelmet, 1);
else
    HandlePlacementFailure(Result); // Inform UI of reason
```

### Expand slots when a bag is equipped

```cpp
void UMyEquipmentBridge::OnBagEquipped(FGameplayTag BagTag)
{
    UMyItemRegistry* Registry = GetWorld()->GetSubsystem<UMyItemRegistry>();
    const int32 BagBonus = Registry->GetBagSlotBonus(BagTag);

    if (USlotCountConstraint* C = InventoryComponent->SlotCountConstraint)
        C->NotifyMaxSlotsChanged(C->GetMaxSlots() + BagBonus);
}
```

### React to inventory changes on the client (UI update)

```cpp
// In a UUserWidget or UActorComponent — client only
void UMyInventoryWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (UInventoryComponent* Inv = GetInventoryComponent())
        Inv->OnInventoryChanged.AddDynamic(this, &UMyInventoryWidget::Refresh);
}

void UMyInventoryWidget::Refresh()
{
    // Rebuild slot display from Inv->Slots.Items
}
```

### Query current weight and capacity

```cpp
float Current = InventoryComponent->GetCurrentWeight();
float Max     = InventoryComponent->WeightConstraint
                    ? InventoryComponent->WeightConstraint->GetMaxWeight() : 0.f;
float Limit   = InventoryComponent->WeightConstraint
                    ? InventoryComponent->WeightConstraint->WeightLimit : 0.f;
```

### Drop an item (server-authoritative)

```cpp
// Called from a validated server RPC.
bool bDropped = InventoryComponent->DropItem(SlotIndex, Quantity);
// Game module picks up GameCoreEvent.Inventory.ItemDropped and spawns the world actor.
```

### Dismantle an item (server-authoritative)

```cpp
bool bDismantled = InventoryComponent->DismantleItem(SlotIndex);
// Game module picks up GameCoreEvent.Inventory.ItemDismantled and grants resources.
```

---

## Gameplay Tags Setup

Add to `DefaultGameplayTags.ini`:

```ini
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemAdded",          DevComment="Item added to an inventory slot")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemRemoved",        DevComment="Item removed from a slot")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemDropped",        DevComment="Item dropped into the world")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ItemDismantled",     DevComment="Item dismantled")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.WeightChanged",      DevComment="Inventory weight changed")
+GameplayTagList=(Tag="GameCoreEvent.Inventory.ConstraintViolated", DevComment="A mutation was blocked by a constraint")
+GameplayTagList=(Tag="RequirementEvent.Inventory.ItemAdded",       DevComment="Watcher: re-evaluate HasItem requirements")
+GameplayTagList=(Tag="RequirementEvent.Inventory.ItemRemoved",     DevComment="Watcher: re-evaluate HasItem requirements")
+GameplayTagList=(Tag="Audit.Inventory.Add")
+GameplayTagList=(Tag="Audit.Inventory.Remove")
+GameplayTagList=(Tag="Audit.Inventory.Drop")
+GameplayTagList=(Tag="Audit.Inventory.Dismantle")
```
