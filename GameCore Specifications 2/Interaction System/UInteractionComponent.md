# `UInteractionComponent`

**File:** `Interaction/Components/InteractionComponent.h/.cpp`

**Data container, state broadcaster, and execution dispatcher.** Added to any actor that can be interacted with — maximum one per actor. Owns entry definitions, replicated runtime state, and per-entry execution delegates.

Game systems bind to `OnInteractionExecuted` on the interactable actor to execute when the server confirms an interaction. They call `SetEntryState` / `SetEntryServerEnabled` to reflect gameplay conditions. The component has no knowledge of the scanner — the relationship is one-way.

---

## Class Definition

```cpp
// File: Interaction/Components/InteractionComponent.h
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/Data/InteractionEntryConfig.h"
#include "Interaction/Data/InteractionEntryDataAsset.h"
#include "Interaction/Data/InteractionNetState.h"
#include "Interaction/Enums/InteractionEnums.h"
#include "InteractionComponent.generated.h"

class UInteractionIconDataAsset;
class URequirementList;
struct FResolvedInteractionOption;

// Fires on ALL machines when any entry's replicated state or bServerEnabled changes.
// Scanner binds here to trigger re-resolution without waiting for the next scan tick.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionStateChanged,
    UInteractionComponent*, Component,
    uint8,                  EntryIndex);

// Fires on the SERVER when an interaction is confirmed and executed.
// Instigator: the pawn that performed the interaction.
// EntryIndex: the flat entry index that was executed.
// Game systems bind here on the interactable actor.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionExecuted,
    APawn*, Instigator,
    uint8,  EntryIndex);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionComponent();  // Calls SetIsReplicatedByDefault(true)

    // ── Entry Storage ─────────────────────────────────────────────────────────
    // Frozen after BeginPlay. Never modify at runtime.

    // DataAsset entries. Shared by reference — no per-component duplication.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entries")
    TArray<TObjectPtr<UInteractionEntryDataAsset>> Entries;

    // One-off inline entries. Zero UObject overhead.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entries")
    TArray<FInteractionEntryConfig> InlineEntries;

    // ── Interaction Distance ──────────────────────────────────────────────────

    // Authoritative on both client (overlap filter) and server (validation).
    // Both measure via GetInteractionLocation().
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction",
        meta = (ClampMin = "50.0", ClampMax = "2000.0"))
    float MaxInteractionDistance = 300.0f;

    // ── Icon Configuration (optional) ─────────────────────────────────────────

    // Maps EInteractableState to display icons. Soft ref — loads on demand.
    // Null is valid — icon resolution falls through to null and widget hides slot.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI")
    TSoftObjectPtr<UInteractionIconDataAsset> IconDataAsset;

private:
    // Delta-serialized. Push Model: dirty-marked per item via MarkItemDirty.
    UPROPERTY(ReplicatedUsing = OnRep_NetStates)
    FInteractionEntryNetStateArray NetStates;

public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // ── State API (server-side only) ──────────────────────────────────────────

    // Set gameplay state of one entry. Valid: Available, Occupied, Cooldown, Disabled.
    // Locked is invalid — silently rejected.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
    void SetEntryState(uint8 EntryIndex, EInteractableState NewState);

    // Administrative enable/disable. Independent of State.
    // When false, entry is invisible to all clients regardless of State.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
    void SetEntryServerEnabled(uint8 EntryIndex, bool bEnabled);

    // Bulk state — one replication delta for all entries.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
    void SetAllEntriesState(EInteractableState NewState);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
    void SetAllEntriesServerEnabled(bool bEnabled);

    // ── Resolution (called by UInteractionManagerComponent, client-side) ───────

    // Evaluates all entries against source/target, applies tag filters and requirements.
    // Stateless and allocation-free — writes into the manager's pre-allocated buffer.
    void ResolveOptions(
        AActor*                             SourceActor,
        AActor*                             TargetActor,
        EResolveMode                        Mode,
        TArray<FResolvedInteractionOption>& OutOptions) const;

    // ── Execution (called by UInteractionManagerComponent, server-side) ────────

    // Dispatches OnInteractionExecuted to bound game system listeners.
    // Only called after all server validation passes.
    void ExecuteEntry(uint8 EntryIndex, APawn* Instigator);

    // ── Query API ─────────────────────────────────────────────────────────────

    // World location used for distance checks on both client and server.
    // Override when interaction point differs from actor pivot.
    // MUST be deterministic and identical on all machines.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
    FVector GetInteractionLocation() const;
    virtual FVector GetInteractionLocation_Implementation() const
    {
        return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
    }

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    const UInteractionIconDataAsset* GetIconDataAsset() const;

    // Returns config at the unified flat index. nullptr if out of range.
    // DataAsset entries first [0..Entries.Num()-1], inline entries after.
    const FInteractionEntryConfig* GetConfigAtIndex(uint8 Index) const;

    // Returns replicated net state for the entry. nullptr if out of range.
    const FInteractionEntryNetState* GetNetStateAtIndex(uint8 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    int32 GetTotalEntryCount() const { return Entries.Num() + InlineEntries.Num(); }

    // ── Delegates ─────────────────────────────────────────────────────────────

    // ALL machines. Fires when any entry's replicated state or bServerEnabled changes.
    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnInteractionStateChanged OnEntryStateChanged;

    // SERVER only. Fires after all validation passes.
    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnInteractionExecuted OnInteractionExecuted;

private:
    UFUNCTION()
    void OnRep_NetStates();

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    void ValidateSingleComponent();
    void ValidateInteractionCollider();
    void ValidateEntryCount();
    void ValidateDataAssetEntries();
#endif
};
```

---

## Replication Setup

```cpp
UInteractionComponent::UInteractionComponent()
{
    SetIsReplicatedByDefault(true);
    // Owning actor must also set bReplicates = true — that is the actor's responsibility.
}

void UInteractionComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // Push Model: NetStates only serializes when MarkItemDirty/MarkArrayDirty is called.
    // Eliminates per-tick dirty checks at steady state across all interactable actors.
    FDoRepLifetimeParams Params;
    Params.bIsPushBased = true;
    DOREPLIFETIME_WITH_PARAMS_FAST(UInteractionComponent, NetStates, Params);
}
```

---

## BeginPlay

```cpp
void UInteractionComponent::BeginPlay()
{
    Super::BeginPlay();

    // Must be set on both server and clients before any replication callbacks fire.
    NetStates.OwningComponent = this;

    if (!HasAuthority()) return;

    const int32 Total = GetTotalEntryCount();
    ensureMsgf(Total <= 255,
        TEXT("[InteractionComponent] %s has %d entries — maximum is 255."),
        *GetOwner()->GetName(), Total);

    NetStates.Items.Reserve(Total);
    for (int32 i = 0; i < FMath::Min(Total, 255); ++i)
    {
        FInteractionEntryNetState& Item = NetStates.Items.AddDefaulted_GetRef();
        Item.EntryIndex     = static_cast<uint8>(i);
        Item.State          = EInteractableState::Available;
        Item.bServerEnabled = true;
    }

    NetStates.MarkArrayDirty(); // Full snapshot for joining clients.

#if !UE_BUILD_SHIPPING
    // Validate requirements are synchronous.
    for (int32 i = 0; i < Total; ++i)
    {
        const FInteractionEntryConfig* Config = GetConfigAtIndex(static_cast<uint8>(i));
        if (Config && Config->EntryRequirements)
        {
            URequirementLibrary::ValidateRequirements(
                Config->EntryRequirements->Requirements,
                Config->EntryRequirements->Authority);
        }
    }
#endif
}
```

---

## State Mutation

```cpp
void UInteractionComponent::SetEntryState(uint8 EntryIndex, EInteractableState NewState)
{
    if (!HasAuthority() || NewState == EInteractableState::Locked) return;
    if (!NetStates.Items.IsValidIndex(EntryIndex)) return;

    FInteractionEntryNetState& Item = NetStates.Items[EntryIndex];
    if (Item.State == NewState) return; // No-op

    Item.State = NewState;
    NetStates.MarkItemDirty(Item);
}

void UInteractionComponent::SetEntryServerEnabled(uint8 EntryIndex, bool bEnabled)
{
    if (!HasAuthority()) return;
    if (!NetStates.Items.IsValidIndex(EntryIndex)) return;

    FInteractionEntryNetState& Item = NetStates.Items[EntryIndex];
    if (Item.bServerEnabled == bEnabled) return;

    Item.bServerEnabled = bEnabled;
    NetStates.MarkItemDirty(Item);
}

void UInteractionComponent::SetAllEntriesState(EInteractableState NewState)
{
    if (!HasAuthority() || NewState == EInteractableState::Locked) return;

    bool bAnyChanged = false;
    for (FInteractionEntryNetState& Item : NetStates.Items)
    {
        if (Item.State != NewState) { Item.State = NewState; bAnyChanged = true; }
    }
    if (bAnyChanged) NetStates.MarkArrayDirty();
}

void UInteractionComponent::SetAllEntriesServerEnabled(bool bEnabled)
{
    if (!HasAuthority()) return;

    bool bAnyChanged = false;
    for (FInteractionEntryNetState& Item : NetStates.Items)
    {
        if (Item.bServerEnabled != bEnabled) { Item.bServerEnabled = bEnabled; bAnyChanged = true; }
    }
    if (bAnyChanged) NetStates.MarkArrayDirty();
}
```

> `MarkItemDirty` sends only the changed item. `MarkArrayDirty` evaluates all items in one pass — use for bulk mutations.

---

## Client-Side Resolution Logic

```cpp
// Pseudocode — see UInteractionComponent.cpp for full implementation.
// Called by UInteractionManagerComponent on every scan and on state-change re-resolve.
void UInteractionComponent::ResolveOptions(
    AActor* SourceActor, AActor* TargetActor,
    EResolveMode Mode,
    TArray<FResolvedInteractionOption>& OutOptions) const
{
    OutOptions.Reset(); // Preserves heap allocation

    for (int32 i = 0; i < GetTotalEntryCount(); ++i)
    {
        const FInteractionEntryNetState* NetState = GetNetStateAtIndex(i);
        const FInteractionEntryConfig*   Config   = GetConfigAtIndex(i);
        if (!NetState || !Config) continue;

        // Hard skips
        if (!NetState->bServerEnabled) continue;
        if (NetState->State == EInteractableState::Disabled) continue;

        EInteractableState CurrentState = NetState->State;
        FText ConditionLabel;

        // Tag pre-filters (bitset AND — near-zero cost)
        // SourceRequiredTags / SourceTagQuery on SourceActor via ITaggedInterface
        // TargetRequiredTags / TargetTagQuery on TargetActor via ITaggedInterface
        // Fail → CurrentState = Locked

        // Requirements (only if not already Locked)
        if (CurrentState != EInteractableState::Locked && Config->EntryRequirements)
        {
            FRequirementContext Context;
            Context.Instigator  = Cast<APawn>(SourceActor);
            Context.PlayerState = Context.Instigator ? Context.Instigator->GetPlayerState() : nullptr;
            Context.World       = GetWorld();

            FRequirementResult Result = Config->EntryRequirements->Evaluate(Context);
            if (!Result.bPassed)
            {
                CurrentState   = EInteractableState::Locked;
                ConditionLabel = Result.FailureReason;
            }
        }

        // Build resolved option (pointers — no copies)
        FResolvedInteractionOption Option;
        Option.SourceComponent  = const_cast<UInteractionComponent*>(this);
        Option.EntryIndex       = static_cast<uint8>(i);
        Option.Label            = &Config->Label;
        Option.InputAction      = Config->InputAction;
        Option.EntryIconOverride= Config->EntryIconOverride;
        Option.InputType        = Config->InputType;
        Option.HoldTimeSeconds  = Config->HoldTimeSeconds;
        Option.GroupTag         = Config->InteractionGroupTag;
        Option.State            = CurrentState;
        Option.ConditionLabel   = ConditionLabel;

        // Resolve UI descriptor
        // (UInteractionDescriptorSubsystem cached on the manager — set externally)
        // Option.UIDescriptor = DescriptorSubsystem->GetOrCreate(Config->UIDescriptorClass);

        OutOptions.Add(Option);
    }

    // Exclusive check: if any Available exclusive candidate exists, suppress all others
    // Group resolution:
    //   Best: per GroupTag → highest OptionPriority non-Locked winner
    //         (if all Locked → show highest OptionPriority Locked entry)
    //   All:  full list sorted by (GroupTag asc, OptionPriority desc)
}
```

---

## Query API

```cpp
const FInteractionEntryConfig* UInteractionComponent::GetConfigAtIndex(uint8 Index) const
{
    const int32 Idx = static_cast<int32>(Index);
    if (Idx < Entries.Num())
    {
        const UInteractionEntryDataAsset* Asset = Entries[Idx];
        return Asset ? &Asset->Config : nullptr;
    }
    const int32 InlineIdx = Idx - Entries.Num();
    return InlineEntries.IsValidIndex(InlineIdx) ? &InlineEntries[InlineIdx] : nullptr;
}

const FInteractionEntryNetState* UInteractionComponent::GetNetStateAtIndex(uint8 Index) const
{
    const int32 Idx = static_cast<int32>(Index);
    return NetStates.Items.IsValidIndex(Idx) ? &NetStates.Items[Idx] : nullptr;
}

const UInteractionIconDataAsset* UInteractionComponent::GetIconDataAsset() const
{
    return IconDataAsset.Get();
}
```

---

## Rep Notify

```cpp
void UInteractionComponent::OnRep_NetStates()
{
    if (!NetStates.OwningComponent)
        NetStates.OwningComponent = this;

    // Fires on initial full-array replication for joining clients.
    // Delta updates are handled by FFastArraySerializer callbacks.
    for (const FInteractionEntryNetState& Item : NetStates.Items)
        OnEntryStateChanged.Broadcast(this, Item.EntryIndex);
}
```

---

## Execution

```cpp
void UInteractionComponent::ExecuteEntry(uint8 EntryIndex, APawn* Instigator)
{
    if (!HasAuthority()) return;
    if (static_cast<int32>(EntryIndex) >= GetTotalEntryCount()) return;

    OnInteractionExecuted.Broadcast(Instigator, EntryIndex);
}
```

---

## Editor Validation

```cpp
#if WITH_EDITOR
void UInteractionComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    ValidateInteractionCollider();
    ValidateSingleComponent();
    ValidateEntryCount();
    ValidateDataAssetEntries();
}

void UInteractionComponent::ValidateSingleComponent()
{
    AActor* Owner = GetOwner();
    if (!Owner) return;
    TArray<UInteractionComponent*> Components;
    Owner->GetComponents(Components);
    if (Components.Num() > 1)
        FMessageLog("PIE").Error(FText::Format(
            NSLOCTEXT("GC", "MultipleInteractionComponents",
                "[InteractionComponent] '{0}' has {1} UInteractionComponent instances. "
                "Only one is allowed per actor — use multiple entries instead."),
            FText::FromString(Owner->GetName()),
            FText::AsNumber(Components.Num())));
}

void UInteractionComponent::ValidateInteractionCollider()
{
    AActor* Owner = GetOwner();
    if (!Owner) return;
    for (UPrimitiveComponent* Prim : TInlineComponentArray<UPrimitiveComponent*>(Owner))
    {
        const ECollisionResponse R =
            Prim->GetCollisionResponseToChannel(ECC_GameTraceChannel_Interaction);
        if (R == ECR_Overlap) return;
        if (R == ECR_Block)
        {
            FMessageLog("PIE").Warning(FText::Format(
                NSLOCTEXT("GC", "BlockCollider",
                    "[InteractionComponent] '{0}': primitive is Block on Interaction channel. "
                    "Use Overlap — Block stops the manager's sweep."),
                FText::FromString(Owner->GetName())));
            return;
        }
    }
    FMessageLog("PIE").Warning(FText::Format(
        NSLOCTEXT("GC", "NoCollider",
            "[InteractionComponent] '{0}' has no primitive set to Overlap on the Interaction "
            "channel. The manager will never detect this actor."),
        FText::FromString(Owner->GetName())));
}

void UInteractionComponent::ValidateEntryCount()
{
    const int32 Total = GetTotalEntryCount();
    if (Total > 255)
        FMessageLog("PIE").Error(FText::Format(
            NSLOCTEXT("GC", "TooManyEntries",
                "[InteractionComponent] '{0}' has {1} entries. Maximum is 255."),
            FText::FromString(GetOwner()->GetName()), FText::AsNumber(Total)));
    if (Total == 0)
        FMessageLog("PIE").Warning(FText::Format(
            NSLOCTEXT("GC", "NoEntries",
                "[InteractionComponent] '{0}' has no entries. Component has no effect."),
            FText::FromString(GetOwner()->GetName())));
    if (MaxInteractionDistance > 1500.0f)
        FMessageLog("PIE").Warning(FText::Format(
            NSLOCTEXT("GC", "LargeRadius",
                "[InteractionComponent] '{0}' MaxInteractionDistance is {1}cm. "
                "Values above 1500cm may undermine distance security."),
            FText::FromString(GetOwner()->GetName()),
            FText::AsNumber(static_cast<int32>(MaxInteractionDistance))));
}

void UInteractionComponent::ValidateDataAssetEntries()
{
    for (int32 i = 0; i < Entries.Num(); ++i)
        if (!Entries[i])
            FMessageLog("PIE").Warning(FText::Format(
                NSLOCTEXT("GC", "NullAsset",
                    "[InteractionComponent] '{0}' has null DataAsset at index {1}."),
                FText::FromString(GetOwner()->GetName()), FText::AsNumber(i)));
}
#endif
```

---

## Constraints

- **One component per actor.** Scanner uses `FindComponentByClass` — a second component is silently ignored. Use entries for multiple options.
- **`GetInteractionLocation()` must be deterministic and identical on all machines.** Reading static transform data (sockets, component offsets) is always safe. Reading replicated state is safe only if the state is in sync before the call.
- **Entry arrays frozen after `BeginPlay`.** Use `SetEntryServerEnabled(false)` or `SetEntryState(Disabled)` to suppress entries dynamically.
