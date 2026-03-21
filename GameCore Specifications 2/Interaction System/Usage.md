# Interaction System — Usage

---

## Making an Actor Interactable

```cpp
// 1. Add UInteractionComponent to any actor (max one per actor)
// 2. Configure entries in the Details panel or in C++
// 3. Add a primitive set to Overlap on the Interaction collision channel
// 4. Bind OnInteractionExecuted to react when the server confirms the interaction

void AMyChest::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>();
        IC->OnInteractionExecuted.AddDynamic(this, &AMyChest::OnInteractionExecuted);
    }
}

void AMyChest::OnInteractionExecuted(APawn* Instigator, uint8 EntryIndex)
{
    // EntryIndex 0 = "Open", EntryIndex 1 = "Examine"
    if (EntryIndex == 0)
        OpenChest(Instigator);
}
```

---

## Enabling Interaction on a Player Pawn

```cpp
// In Pawn Blueprint or C++:
// 1. Add UInteractionManagerComponent
// 2. Bind Enhanced Input actions to RequestInteract / RequestInteractRelease
// 3. Bind delegates to drive UI

void AMyPlayerPawn::BeginPlay()
{
    Super::BeginPlay();

    if (!IsLocallyControlled()) return;

    UInteractionManagerComponent* IM = FindComponentByClass<UInteractionManagerComponent>();

    IM->OnResolvedOptionsChanged.AddDynamic(
        this, &AMyPlayerPawn::OnInteractionOptionsChanged);

    IM->OnHoldProgressChanged.AddDynamic(
        this, &AMyPlayerPawn::OnHoldProgress);

    IM->OnInteractionRejected.AddDynamic(
        this, &AMyPlayerPawn::OnInteractionRejected);
}

// Enhanced Input binding (in SetupPlayerInputComponent or input config):
// IA_Interact → pressed: IM->RequestInteract(0)
//             → released: IM->RequestInteractRelease()
```

---

## Authoring Entries — DataAsset

Create a `UInteractionEntryDataAsset` in the Content Browser and configure its fields:

| Field | Example |
|---|---|
| `Label` | "Open" |
| `InputType` | Press |
| `InteractionGroupTag` | `Interaction.Group.Primary` |
| `OptionPriority` | 200 |
| `SourceRequiredTags` | `Interaction.Source.IsPlayer` |
| `TargetRequiredTags` | (empty) |
| `EntryRequirements` | `(null)` — or assign a `URequirementList` asset |

Reference the asset in `UInteractionComponent::Entries`.

---

## Authoring Entries — Inline

```cpp
// Inline entries for one-off interactions that don't warrant a named asset.
// Configure in the Details panel under InlineEntries.
// Zero UObject overhead — no GC pressure beyond the component.

FInteractionEntryConfig Config;
Config.Label        = NSLOCTEXT("GC", "Examine", "Examine");
Config.InputType    = EInteractionInputType::Press;
Config.OptionPriority = 100;
// Config.EntryRequirements = ... assign a URequirementList asset reference

InteractionComponent->InlineEntries.Add(Config);
// Must be called before BeginPlay — arrays are frozen after BeginPlay.
```

---

## Controlling Entry State from Game Systems

```cpp
// Server-side only.

UInteractionComponent* IC = OreNode->FindComponentByClass<UInteractionComponent>();

// Mark as occupied while a player is harvesting (EntryIndex 0)
IC->SetEntryState(0, EInteractableState::Occupied);

// Mark available again after harvest
IC->SetEntryState(0, EInteractableState::Available);

// Put on cooldown
IC->SetEntryState(0, EInteractableState::Cooldown);

// Administrative kill switch (live-ops, bug mitigation) — does not affect State
IC->SetEntryServerEnabled(0, false);

// Bulk: mark all entries Occupied at once (one replication delta)
IC->SetAllEntriesState(EInteractableState::Occupied);
```

---

## Requirements on an Entry

```cpp
// In FInteractionEntryConfig, assign a URequirementList asset reference.
// The list is evaluated client-side (for Locked display) with ClientValidated authority.
// It is re-evaluated server-side (authoritative) during ServerRequestInteract.
//
// Example: "Requires Golden Key" (tag-based requirement)
// 1. Create URequirementList asset
// 2. Set Authority = ClientValidated
// 3. Add URequirement_HasTag child: RequiredTag = Item.Key.Golden
// 4. Assign the list to FInteractionEntryConfig::EntryRequirements

// Requirements must be synchronous (IsAsync() == false).
// Detected at BeginPlay via URequirementLibrary::ValidateRequirements.
```

---

## Tag Filtering

```cpp
// FInteractionEntryConfig fields:

// Fast path — bitset AND. Prefer for the majority of gates.
Config.SourceRequiredTags.AddTag(FGameplayTag::RequestGameplayTag("Player.Class.Pirate"));
Config.TargetRequiredTags.AddTag(FGameplayTag::RequestGameplayTag("Ship.State.Docked"));

// Advanced path — AND/OR/NOT query. Only when bitset is insufficient.
Config.SourceTagQuery = FGameplayTagQuery::BuildQuery(
    FGameplayTagQueryExpression().AnyTagsMatch()
        .AddTag(FGameplayTag::RequestGameplayTag("Player.Rank.Captain"))
        .AddTag(FGameplayTag::RequestGameplayTag("Player.Rank.Admiral")));
```

---

## Suppressing Interaction by Tag

```cpp
// UInteractionManagerComponent::DisablingTags
// When the pawn owns any tag in this container, scanning stops and current best is cleared.

// Configure in Details panel or C++:
InteractionManagerComponent->DisablingTags.AddTag(
    FGameplayTag::RequestGameplayTag("State.Dead"));
InteractionManagerComponent->DisablingTags.AddTag(
    FGameplayTag::RequestGameplayTag("State.InCutscene"));
```

---

## Overriding Interaction Location

```cpp
// Default: actor pivot.
// Override when the meaningful interaction point differs from the pivot.
// Both client distance filter and server validation call this method — they always agree.

FVector AMyShipHelm::GetInteractionLocation_Implementation() const
{
    // Return the helm wheel socket location, not the actor origin
    return GetMesh()->GetSocketLocation(TEXT("HelmSocket"));
}
```

---

## Highlighting Interactable Actors

```cpp
// Add UHighlightComponent to any actor that should highlight when focused.
// UInteractionManagerComponent calls SetHighlightActive automatically on best-change.

// Configure stencil value to control highlight color/style:
// (Consumed by the project's post-process outline material)
//   1 = generic interactable
//   2 = NPC
//   3 = item / loot
//   4 = quest objective

// HighlightComponent->StencilValue = 2; // in Details panel or C++
```

---

## Contextual UI Descriptor

```cpp
// 1. Create a UInteractionUIDescriptor subclass in game code (not in GameCore)
UCLASS()
class UMyNPCDescriptor : public UInteractionUIDescriptor
{
    GENERATED_BODY()
public:
    virtual void PopulateContextWidget_Implementation(
        UInteractionContextWidget* Widget,
        const FResolvedInteractionOption& Option,
        AActor* Interactable) const override
    {
        if (!Interactable) return;
        // Read live data from the NPC and populate widget slots
        UNPCComponent* NPC = Interactable->FindComponentByClass<UNPCComponent>();
        if (NPC)
            Widget->SetNameText(NPC->GetDisplayName());
    }
};

// 2. Assign the descriptor class to FInteractionEntryConfig::UIDescriptorClass
//    (in the DataAsset Details panel)

// 3. In the interaction widget:
//    Bind OnResolvedOptionsChanged
//    For each FResolvedInteractionOption:
//      if (Option.UIDescriptor)
//          Option.UIDescriptor->PopulateContextWidget(this, Option, TargetActor);
```

---

## Hold Interaction

```cpp
// Configure in FInteractionEntryConfig:
Config.InputType     = EInteractionInputType::Hold;
Config.HoldTimeSeconds = 2.0f;

// Bind hold progress for UI:
IM->OnHoldProgressChanged.AddDynamic(this, &UMyWidget::OnHoldProgress);
// Progress is [0.0, 1.0]. UI drives a progress bar from this.

// Bind cancel feedback:
IM->OnHoldCancelled.AddDynamic(this, &UMyWidget::OnHoldCancelled);
// EHoldCancelReason: InputReleased, PlayerMoved, DisabledByTag, TargetChanged, TargetLost
```

---

## Debug Visualization

```
// In console or PIE:
gc.Interaction.Debug 1   // Scan sphere + best candidate
gc.Interaction.Debug 2   // All candidates with scores, states, labels
gc.Interaction.Debug 0   // Off
```

---

## Exclusive Interactions

```cpp
// An exclusive entry suppresses all other entries when it wins.
// Use for interactions that demand undivided player attention.
Config.bExclusive = true;
Config.OptionPriority = 500;  // Wins any priority contest

// When this entry is Available and has the highest OptionPriority among exclusive
// candidates, it is the only resolved option — all others are suppressed.
```
