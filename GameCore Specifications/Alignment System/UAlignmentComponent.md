# UAlignmentComponent

**Sub-page of:** [Alignment System](../Alignment%20System.md)

`UAlignmentComponent` is an `ActorComponent` that lives on `APlayerState`. It owns the replicated runtime state for all alignment axes registered for that player, executes server-authoritative batch mutations, and fires the GMS event after each batch.

**File:** `Alignment/AlignmentComponent.h / .cpp`

---

## Class Definition

```cpp
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent),
       DisplayName = "Alignment Component")
class GAMECORE_API UAlignmentComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAlignmentComponent();

    // ── Setup ──────────────────────────────────────────────────────────────────

    // Register an alignment axis definition for this player.
    // Safe to call multiple times with the same definition — duplicate registrations are ignored.
    // Initializes the underlying value to 0 if no entry exists yet.
    // Must be called server-side before any ApplyAlignmentDeltas call for that axis.
    UFUNCTION(BlueprintCallable, Category = "Alignment", BlueprintAuthorityOnly)
    void RegisterAlignment(UAlignmentDefinition* Definition);

    // ── Mutation ───────────────────────────────────────────────────────────────

    // Apply one or more alignment deltas in a single atomic batch.
    //
    // Per-axis evaluation order:
    //   1. Skip if Definition not registered (silent — not a design error at runtime).
    //   2. Evaluate Definition->ChangeRequirements against Context (if set).
    //      Skip this axis if requirements fail.
    //   3. Add Delta to UnderlyingValue, clamped to [SaturatedMin, SaturatedMax].
    //   4. If the clamped result equals the previous value (no movement), skip.
    //   5. MarkItemDirty on the changed entry.
    //
    // After all axes are processed:
    //   - If any axis changed: fire one FAlignmentChangedMessage on GMS.
    //   - If no axis changed: no GMS broadcast.
    //
    // Context is caller-constructed. The caller is responsible for populating
    // Subject (typically the owning APlayerState or APawn) and any payload data
    // required by the ChangeRequirements in the registered definitions.
    UFUNCTION(BlueprintCallable, Category = "Alignment", BlueprintAuthorityOnly)
    void ApplyAlignmentDeltas(
        const TArray<FAlignmentDelta>& Deltas,
        const FRequirementContext& Context);

    // ── Query ──────────────────────────────────────────────────────────────────

    // Returns the effective alignment value for the given axis.
    // Effective = Clamp(UnderlyingValue, EffectiveMin, EffectiveMax).
    // Returns 0 if the axis is not registered. Safe to call on both server and client.
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Alignment")
    float GetEffectiveAlignment(FGameplayTag AlignmentTag) const;

    // Returns the raw underlying accumulated value.
    // Use for persistence (save/load) and debug display.
    // Returns 0 if the axis is not registered.
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Alignment")
    float GetUnderlyingAlignment(FGameplayTag AlignmentTag) const;

    // Returns true if the axis is registered on this component.
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Alignment")
    bool IsAlignmentRegistered(FGameplayTag AlignmentTag) const;

    // ── Persistence Support ────────────────────────────────────────────────────

    // Restores underlying values from a save record.
    // Call before the first replication tick (e.g. immediately after RegisterAlignment
    // calls, before the player state is fully replicated to clients).
    // Silently ignores tags not currently registered.
    UFUNCTION(BlueprintCallable, Category = "Alignment", BlueprintAuthorityOnly)
    void LoadFromSave(const TArray<FAlignmentSaveEntry>& SaveEntries);

    // Fills OutEntries with the current underlying values for all registered axes.
    // Use to build a save record.
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Alignment")
    void GetSaveEntries(TArray<FAlignmentSaveEntry>& OutEntries) const;

private:
    // Replicated runtime data. Only dirty items travel over the wire.
    UPROPERTY(ReplicatedUsing = OnRep_AlignmentData)
    FAlignmentArray AlignmentData;

    // Definitions are not replicated — data assets load identically on all machines.
    // Key = AlignmentTag. Only populated on the server.
    UPROPERTY()
    TMap<FGameplayTag, TObjectPtr<UAlignmentDefinition>> Definitions;

    UFUNCTION()
    void OnRep_AlignmentData();

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
```

---

## `RegisterAlignment` Implementation

```cpp
void UAlignmentComponent::RegisterAlignment(UAlignmentDefinition* Definition)
{
    if (!ensure(GetOwner()->HasAuthority())) return;
    if (!ensure(Definition && Definition->AlignmentTag.IsValid())) return;

    const FGameplayTag Tag = Definition->AlignmentTag;

    if (Definitions.Contains(Tag))
    {
        // Already registered. Idempotent — no duplicate entries.
        return;
    }

    Definitions.Add(Tag, Definition);

    // Add a runtime entry initialized to zero if not already present
    // (LoadFromSave may have already seeded the value).
    if (!AlignmentData.FindByTag(Tag))
    {
        FAlignmentEntry& NewEntry = AlignmentData.Items.AddDefaulted_GetRef();
        NewEntry.AlignmentTag    = Tag;
        NewEntry.UnderlyingValue = 0.f;
        AlignmentData.MarkItemDirty(NewEntry);
    }
}
```

---

## `ApplyAlignmentDeltas` Implementation

```cpp
void UAlignmentComponent::ApplyAlignmentDeltas(
    const TArray<FAlignmentDelta>& Deltas,
    const FRequirementContext& Context)
{
    check(GetOwner()->HasAuthority());

    FAlignmentChangedMessage Msg;
    Msg.PlayerState = Cast<APlayerState>(GetOwner());

    for (const FAlignmentDelta& Delta : Deltas)
    {
        if (FMath::IsNearlyZero(Delta.Delta)) continue;

        UAlignmentDefinition* Def = Definitions.FindRef(Delta.AlignmentTag);
        if (!Def)
        {
            // Axis not registered — caller error, but silently skip at runtime.
            UE_LOG(LogAlignment, Warning,
                TEXT("ApplyAlignmentDeltas: tag '%s' not registered on %s — skipped."),
                *Delta.AlignmentTag.ToString(), *GetOwner()->GetName());
            continue;
        }

        // Per-axis requirement check.
        if (Def->ChangeRequirements)
        {
            const FRequirementResult Result = Def->ChangeRequirements->Evaluate(Context);
            if (!Result.bPassed)
            {
                // Requirements failed — skip this axis, continue batch.
                continue;
            }
        }

        FAlignmentEntry* Entry = AlignmentData.FindByTag(Delta.AlignmentTag);
        if (!Entry) continue; // Should not happen if Definitions and AlignmentData are in sync.

        const float OldUnderlying = Entry->UnderlyingValue;
        Entry->UnderlyingValue = FMath::Clamp(
            Entry->UnderlyingValue + Delta.Delta,
            Def->SaturatedMin,
            Def->SaturatedMax);

        const float AppliedDelta = Entry->UnderlyingValue - OldUnderlying;
        if (FMath::IsNearlyZero(AppliedDelta))
        {
            // No movement (already saturated in that direction).
            continue;
        }

        AlignmentData.MarkItemDirty(*Entry);

        FAlignmentChangedEntry& ChangeEntry = Msg.Changes.AddDefaulted_GetRef();
        ChangeEntry.AlignmentTag  = Delta.AlignmentTag;
        ChangeEntry.AppliedDelta  = AppliedDelta;
        ChangeEntry.NewUnderlying = Entry->UnderlyingValue;
        ChangeEntry.NewEffective  = Entry->GetEffectiveValue(*Def);
    }

    if (Msg.Changes.IsEmpty()) return;

    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
    {
        Bus->Broadcast(
            GameCoreEventTags::Alignment_Changed,
            Msg,
            EGameCoreEventScope::ServerOnly);
    }
}
```

---

## Query Implementations

```cpp
float UAlignmentComponent::GetEffectiveAlignment(FGameplayTag AlignmentTag) const
{
    const FAlignmentEntry* Entry = AlignmentData.FindByTag(AlignmentTag);
    if (!Entry) return 0.f;

    const UAlignmentDefinition* Def = Definitions.FindRef(AlignmentTag);
    if (!Def) return 0.f; // Client does not have Definitions — use raw clamp from replicated range.
    // Note: On clients, Def will be null. For client-side UI queries,
    // callers should cache the definition asset directly from their data table
    // and pass it explicitly, or use GetUnderlyingAlignment and apply their own clamp.

    return Entry->GetEffectiveValue(*Def);
}

float UAlignmentComponent::GetUnderlyingAlignment(FGameplayTag AlignmentTag) const
{
    const FAlignmentEntry* Entry = AlignmentData.FindByTag(AlignmentTag);
    return Entry ? Entry->UnderlyingValue : 0.f;
}
```

> **Client query note.** `Definitions` is only populated on the server (where `RegisterAlignment` is called). Clients have access to `AlignmentData` (replicated) but not to `Definitions`. For client-side UI that needs effective values, the game layer should either:
> - Have the UI subsystem hold a reference to the definition assets directly (loaded via asset manager), or
> - Replicate a derived `EffectiveValue` float per entry if the query is frequent enough to justify it.

---

## Replication Setup

```cpp
void UAlignmentComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UAlignmentComponent, AlignmentData);
}

void UAlignmentComponent::OnRep_AlignmentData()
{
    // Notify owning client's UI or local subsystems that alignment data changed.
    // Use a delegate here if the game layer needs to react.
    // GMS is NOT broadcast on the client — server-side GMS covers authoritative listeners.
    OnAlignmentDataReplicated.Broadcast();
}
```

Declare the delegate in the header:

```cpp
// Fired on the owning client after any replicated alignment update arrives.
// Use for UI refresh. Not fired on the server.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAlignmentDataReplicated);

UPROPERTY(BlueprintAssignable, Category = "Alignment|Delegates")
FOnAlignmentDataReplicated OnAlignmentDataReplicated;
```

---

## Mutation Flow (Step by Step)

```
Caller (server)
  │
  ├─ Constructs TArray<FAlignmentDelta>  (one or more axes)
  ├─ Constructs FRequirementContext       (Subject = PlayerState / APawn)
  └─ Calls AlignmentComp->ApplyAlignmentDeltas(Deltas, Context)
        │
        ├─ For each FAlignmentDelta:
        │     ├─ Lookup Definition by tag  → skip if not registered
        │     ├─ Evaluate ChangeRequirements (if set)  → skip axis if fail
        │     ├─ Add Delta to UnderlyingValue, clamp to [SaturatedMin, SaturatedMax]
        │     ├─ Compute AppliedDelta = new - old  → skip if zero
        │     └─ MarkItemDirty, append to Msg.Changes
        │
        └─ If Msg.Changes not empty:
              └─ UGameCoreEventSubsystem::Broadcast(Alignment_Changed, Msg, ServerOnly)

FFastArraySerializer
  └─ Replicates dirty FAlignmentEntry items to owning client
        └─ OnRep_AlignmentData fires → UI refresh delegate
```

---

## Important Notes

- **`Definitions` is server-only.** Clients must not call `RegisterAlignment`. Definitions are populated during server-side setup only.
- **`AlignmentData` is replicated to the owning client only** (condition `COND_OwnerOnly` recommended if only the owning player needs their alignment).
- **Thread safety.** All mutation paths are game-thread only. No async mutation is expected or supported.
- **Empty deltas.** A delta of exactly `0.f` is skipped before any lookup — avoids unnecessary requirement evaluation.
- **No partial rollback.** If axis 1 succeeds and axis 2 fails requirements, axis 1's change is kept. Batches are not atomic across axes — each axis is independent by design.
