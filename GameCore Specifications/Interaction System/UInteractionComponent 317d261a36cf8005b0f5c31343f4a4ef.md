# UInteractionComponent

**Sub-page of:** [Interaction System — Enhanced Specification](../Interaction%20System%20317d261a36cf8196ae77fc3c2e1e352d.md)

`UInteractionComponent` is a **data container and state broadcaster** — added to any actor that can be interacted with. It owns the entry definitions and the replicated runtime state. It has no knowledge of who is interacting, no validation logic, and no RPCs. All interaction logic lives in `UInteractionManagerComponent` on the initiating pawn.

Game systems that need to react to an interaction (open a shop, start dialogue, grant a resource) bind to `UInteractionManagerComponent::OnInteractionConfirmed`, which carries a reference to the component and entry index.

**Files:** `Interaction/Components/InteractionComponent.h / .cpp`

---

# Class Definition

```cpp
// Fired on all machines when any entry's replicated state or bServerEnabled changes.
// UInteractionManagerComponent binds here to trigger re-resolution on state changes
// without waiting for the next scan tick.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionStateChanged,
    UInteractionComponent*, Component,
    uint8,                  EntryIndex);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionComponent();  // Calls SetIsReplicatedByDefault(true) — see Replication Setup

    // ── Entry Storage (static, never replicated) ──────────────────────────────

    // Reusable DataAsset-backed entries. Shared by reference — no per-component duplication.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entries")
    TArray<TObjectPtr<UInteractionEntryDataAsset>> Entries;

    // One-off inline entries. Zero UObject overhead. Prefer DataAssets for anything reused.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entries")
    TArray<FInteractionEntryConfig> InlineEntries;

    // ── Interaction Distance ──────────────────────────────────────────────────

    // Maximum distance at which this component can be interacted with.
    // Authoritative on both the client manager (overlap filter) and the server (validation).
    // Default 300cm suits most human-scale actors. Increase for large actors (ships, buildings,
    // resource nodes) where the interaction collider extends beyond the actor pivot.
    // ClampMax 2000cm prevents accidental global-reach values that undermine distance security.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction",
        meta = (ClampMin = "50.0", ClampMax = "2000.0"))
    float MaxInteractionDistance = 300.0f;

    // ── Icon Configuration (optional) ─────────────────────────────────────────

    // Maps EInteractableState values to display icons for this component's prompts.
    // Soft reference — loads on demand when a widget first requests an icon.
    // Null is valid — icon resolution falls through to null and the widget hides the slot.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI",
        meta = (AllowedTypes = "InteractionIconDataAsset"))
    TSoftObjectPtr<UInteractionIconDataAsset> IconDataAsset;

private:
    // ── Replicated Runtime State ──────────────────────────────────────────────

    // Delta-serialized via FFastArraySerializer.
    // Push Model: dirty-marked per item via MarkItemDirty — not per full array.
    // OnRep_NetStates fires on initial full-array replication to new clients.
    UPROPERTY(ReplicatedUsing = OnRep_NetStates)
    FInteractionEntryNetStateArray NetStates;

public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // ── State API (server-side only) ──────────────────────────────────────────

    // Set the gameplay state of an entry. Replicates to all clients via FFastArraySerializer delta.
    // Valid values: Available, Occupied, Cooldown, Disabled.
    // Do NOT pass Locked — Locked is a client-only evaluation result, never server-set.
    // Silently ignored for out-of-bounds indices.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
    void SetEntryState(uint8 EntryIndex, EInteractableState NewState);

    // Administrative enable/disable, independent of gameplay State.
    // When false the entry is invisible to all clients regardless of State.
    // Does not disturb the State value.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
    void SetEntryServerEnabled(uint8 EntryIndex, bool bEnabled);

    // Convenience: set the same state on all entries in one replication delta.
    // Preferred over looping SetEntryState when transitioning a whole component
    // (e.g. mark all entries Occupied when a player boards a helm).
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
    void SetAllEntriesState(EInteractableState NewState);

    // Convenience: enable or disable all entries in one replication delta.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
    void SetAllEntriesServerEnabled(bool bEnabled);

    // ── Resolution (client-side, called by UInteractionManagerComponent) ──────

    // Evaluates all entries against source and target, applies tag filters and condition
    // providers, writes results into OutOptions according to Mode.
    // Stateless and allocation-free — writes into the manager's pre-allocated buffer.
    void ResolveOptions(
        AActor*                             SourceActor,
        AActor*                             TargetActor,
        EResolveMode                        Mode,
        TArray<FResolvedInteractionOption>& OutOptions) const;

    // ── Query API (any machine) ───────────────────────────────────────────────

    // Returns the icon DataAsset if assigned and loaded, or nullptr.
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    const UInteractionIconDataAsset* GetIconDataAsset() const;

    // Returns a pointer to the config at the unified flat index, or nullptr if out of range.
    // Promotes Index to int32 internally to avoid uint8 truncation in boundary comparisons.
    const FInteractionEntryConfig* GetConfigAtIndex(uint8 Index) const;

    // Returns the replicated net state for the entry at the given index, or nullptr if out of range.
    const FInteractionEntryNetState* GetNetStateAtIndex(uint8 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    int32 GetTotalEntryCount() const { return Entries.Num() + InlineEntries.Num(); }

    // ── Delegate ──────────────────────────────────────────────────────────────

    // ALL machines. Fires when any entry's replicated state or bServerEnabled changes.
    // UInteractionManagerComponent binds here to re-resolve options without a full rescan.
    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnInteractionStateChanged OnEntryStateChanged;

private:
    UFUNCTION()
    void OnRep_NetStates();
};
```

> **No RPCs, no validation, no interaction outcome delegates.** This component does not know who interacts with it or whether an interaction succeeded. `UInteractionManagerComponent` on the initiating pawn owns the full interaction lifecycle — scanning, validation, confirmation, and outcome broadcast.
> 

> **`SetAllEntriesState` / `SetAllEntriesServerEnabled` use a single `MarkArrayDirty` call.** Looping `SetEntryState` across N entries generates N `MarkItemDirty` calls and N separate replication deltas. The bulk variants mutate all items then call `MarkArrayDirty()` once — one packet regardless of entry count.
> 

---

# Replication Setup

```cpp
UInteractionComponent::UInteractionComponent()
{
    // Component must be explicitly opt-in for replication.
    // The owning actor must also set bReplicates = true — that is the actor's responsibility.
    SetIsReplicatedByDefault(true);
}

void UInteractionComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // Push Model: NetStates is only serialized when an item is marked dirty via
    // MarkItemDirty or MarkArrayDirty. Eliminates per-tick dirty checks on all
    // interactable actors at steady state — critical for MMO-scale actor counts.
    FDoRepLifetimeParams Params;
    Params.bIsPushBased = true;
    DOREPLIFETIME_WITH_PARAMS_FAST(UInteractionComponent, NetStates, Params);
}
```

`GameCore.Build.cs` must include `"NetCore"` in `PrivateDependencyModuleNames` for Push Model support.

---

# BeginPlay Initialization

```cpp
void UInteractionComponent::BeginPlay()
{
    Super::BeginPlay();

    // Must be set on both server and clients before any replication callbacks fire.
    // PostReplicatedAdd null-checks this defensively, but setting it here ensures
    // correctness for replication that arrives after BeginPlay.
    NetStates.OwningComponent = this;

    if (!HasAuthority())
        return;

    // Populate one net state item per entry. Entry arrays are frozen from this point.
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

    // Full snapshot for joining clients — subsequent changes use MarkItemDirty.
    NetStates.MarkArrayDirty();
}
```

---

# State Mutation

```cpp
void UInteractionComponent::SetEntryState(uint8 EntryIndex, EInteractableState NewState)
{
    if (!HasAuthority() || NewState == EInteractableState::Locked) return;
    if (!NetStates.Items.IsValidIndex(EntryIndex)) return;

    FInteractionEntryNetState& Item = NetStates.Items[EntryIndex];
    if (Item.State == NewState) return;  // No-op — no dirty mark, no replication

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

> **`MarkItemDirty` vs `MarkArrayDirty`.** `MarkItemDirty` sends only the changed item in the next delta. `MarkArrayDirty` evaluates all items in one pass — use for bulk mutations where multiple items change simultaneously.
> 

---

# Client-Side Resolution Logic

Called by `UInteractionManagerComponent` on every scan and on state-change re-resolve. Stateless and allocation-free — writes into the manager's pre-allocated `ResolvedBuffer`.

```
ResolveOptions(SourceActor, TargetActor, Mode, OUT OutOptions):

  OutOptions.Reset()    // Preserves heap allocation

  For i in [0 .. GetTotalEntryCount() - 1]:

    NetState = GetNetStateAtIndex(i)
    Config   = GetConfigAtIndex(i)
    if (!NetState || !Config) continue

    // Hard skips
    [a] !NetState->bServerEnabled   → continue
    [b] State == Disabled           → continue

    CurrentState          = NetState->State
    ConditionLabel        = FText::GetEmpty()
    ConditionIconOverride = null

    // Tag pre-filters — bitset AND, near-zero cost
    [c] SourceRequiredTags non-empty AND NOT SourceActor.HasAll  → CurrentState = Locked
        SourceTagQuery non-empty AND does not match              → CurrentState = Locked

    [d] TargetRequiredTags non-empty AND NOT TargetActor.HasAll  → CurrentState = Locked
        TargetTagQuery non-empty AND does not match              → CurrentState = Locked

    // Condition providers — only if not already Locked
    [e] If CurrentState != Locked:
            If SourceActor implements IInteractionConditionProvider:
                Result = CanSeeInteraction(TargetActor, *Config)
                If !Result.bCanProceed → CurrentState = Locked, store label + icon

            If still not Locked AND TargetActor implements IInteractionConditionProvider:
                Result = CanSeeInteraction(SourceActor, *Config)
                If !Result.bCanProceed → CurrentState = Locked, store label + icon

    [f] Append candidate:
        { SourceComponent=this, EntryIndex=i, Label=&Config->Label,
          InputAction, EntryIconOverride, InputType, HoldTimeSeconds,
          GroupTag, State=CurrentState, ConditionLabel, ConditionIconOverride }

  // Exclusive check
  [g] If any candidate: bExclusive == true AND State == Available:
          OutOptions = { highest OptionPriority such candidate }; return

  // Group resolution
  [h] Best: per GroupTag → highest OptionPriority non-Locked winner
            (if all Locked → show highest OptionPriority Locked entry greyed-out)
      All:  all candidates sorted by (GroupTag asc, OptionPriority desc)
```

---

# Query API Implementation

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

# Rep Notify: `OnRep_NetStates`

Fires on clients for the initial full-array replication. Delta updates are handled by `FFastArraySerializer` callbacks (`PostReplicatedAdd`, `PostReplicatedChange`).

```cpp
void UInteractionComponent::OnRep_NetStates()
{
    if (!NetStates.OwningComponent)
        NetStates.OwningComponent = this;

    for (const FInteractionEntryNetState& Item : NetStates.Items)
        OnEntryStateChanged.Broadcast(this, Item.EntryIndex);
}
```

---

# Editor Validation

```cpp
#if WITH_EDITOR
void UInteractionComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    ValidateInteractionCollider();
    ValidateEntryCount();
    ValidateDataAssetEntries();
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

# Known Constraints

**Pivot-based distance check.** The manager compares pawn location to the actor pivot. For large actors, increase `MaxInteractionDistance` to compensate. The 75cm server-side tolerance covers latency desync — it is not a substitute for correct distance configuration.

**Entry arrays frozen after `BeginPlay`.** Do not add, remove, or reorder entries at runtime. Use `SetEntryServerEnabled(false)` or `SetEntryState(Disabled)` to suppress entries dynamically.