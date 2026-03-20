# UAlignmentComponent

**File:** `GameCore/Source/GameCore/Alignment/AlignmentComponent.h` / `.cpp`

`UAlignmentComponent` is a `UActorComponent` that lives on `APlayerState`. It owns the replicated runtime state for all alignment axes registered for that player, executes server-authoritative batch mutations, and fires the GMS event after each batch. It implements `IPersistableComponent` to participate in the serialization lifecycle.

---

## GMS Event Message Types

Message structs are defined in this header (following the v2 convention: structs live with the system that owns them).

```cpp
/** One axis record within a batch alignment change broadcast. */
USTRUCT(BlueprintType)
struct GAMECORE_API FAlignmentChangedEntry
{
    GENERATED_BODY()

    /** The axis that changed. */
    UPROPERTY() FGameplayTag AlignmentTag;

    /**
     * Actual delta applied after saturation clamping.
     * May differ from the requested delta if the underlying value hit SaturatedMin/Max.
     * Never zero — entries with zero applied delta are excluded from the broadcast.
     */
    UPROPERTY() float AppliedDelta  = 0.f;

    /** New underlying value after mutation. */
    UPROPERTY() float NewUnderlying = 0.f;

    /**
     * New effective value = Clamp(NewUnderlying, EffectiveMin, EffectiveMax).
     * Pre-computed server-side so listeners do not need the definition asset.
     */
    UPROPERTY() float NewEffective  = 0.f;
};

/**
 * Broadcast once per ApplyAlignmentDeltas call.
 * Contains all axes that actually changed in that batch.
 * Never broadcast with an empty Changes array.
 *
 * Channel:  GameCoreEvent.Alignment.Changed
 * Scope:    ServerOnly
 * Origin:   UAlignmentComponent::ApplyAlignmentDeltas (server only)
 * Client reaction: clients observe via FFastArraySerializer replication.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FAlignmentChangedMessage
{
    GENERATED_BODY()

    /** The player whose alignment changed. TWeakObjectPtr per message struct authoring rules. */
    UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;

    /** All axes that actually changed in this batch. Guaranteed non-empty when broadcast fires. */
    UPROPERTY() TArray<FAlignmentChangedEntry> Changes;
};
```

---

## Class Declaration

```cpp
// AlignmentComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Alignment/AlignmentDefinition.h"
#include "Alignment/AlignmentTypes.h"
#include "Persistence/PersistableComponent.h"
#include "AlignmentComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAlignmentDataReplicated);

/**
 * Owns all alignment axis runtime data for one APlayerState.
 * Server-authoritative mutations via ApplyAlignmentDeltas.
 * Clients observe alignment changes via FFastArraySerializer replication.
 * Implements IPersistableComponent for automatic save/load.
 */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent),
       DisplayName = "Alignment Component")
class GAMECORE_API UAlignmentComponent : public UActorComponent,
                                         public IPersistableComponent
{
    GENERATED_BODY()
public:
    UAlignmentComponent();

    // ── Setup ──────────────────────────────────────────────────────────────────

    /**
     * Register an alignment axis definition for this player.
     * Idempotent — duplicate registrations are ignored.
     * Initializes UnderlyingValue to 0 if no entry exists yet.
     * Copies EffectiveMin/Max from the definition onto the FAlignmentEntry so that
     * clients can call GetEffectiveAlignment without access to the Definitions map.
     * Must be called server-side before any ApplyAlignmentDeltas call for that axis.
     */
    UFUNCTION(BlueprintCallable, Category = "Alignment", BlueprintAuthorityOnly)
    void RegisterAlignment(UAlignmentDefinition* Definition);

    // ── Mutation ───────────────────────────────────────────────────────────────

    /**
     * Apply one or more alignment deltas in a single atomic batch.
     *
     * Per-axis evaluation order:
     *   1. Skip if Delta == 0.
     *   2. Skip if Definition not registered (logs warning).
     *   3. Evaluate Definition->ChangeRequirements against Context (if set).
     *      Skip this axis if requirements fail.
     *   4. Add Delta to UnderlyingValue, clamp to [SaturatedMin, SaturatedMax].
     *   5. If clamped result equals previous value (fully saturated), skip.
     *   6. MarkItemDirty. Append to Msg.Changes.
     *
     * After all axes:
     *   - If any axis changed: fire FAlignmentChangedMessage on UGameCoreEventBus.
     *   - Call NotifyDirty for persistence.
     */
    UFUNCTION(BlueprintCallable, Category = "Alignment", BlueprintAuthorityOnly)
    void ApplyAlignmentDeltas(
        const TArray<FAlignmentDelta>& Deltas,
        const FRequirementContext& Context);

    // ── Query ──────────────────────────────────────────────────────────────────

    /**
     * Returns the effective alignment value: Clamp(UnderlyingValue, EffectiveMin, EffectiveMax).
     * Safe on both server and client.
     * Returns 0 if the axis is not registered.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Alignment")
    float GetEffectiveAlignment(FGameplayTag AlignmentTag) const;

    /**
     * Returns the raw underlying accumulated value.
     * Use for persistence debugging only — prefer GetEffectiveAlignment for game logic.
     * Returns 0 if the axis is not registered.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Alignment")
    float GetUnderlyingAlignment(FGameplayTag AlignmentTag) const;

    /** Returns true if the axis is registered on this component. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Alignment")
    bool IsAlignmentRegistered(FGameplayTag AlignmentTag) const;

    // ── Client Delegate ────────────────────────────────────────────────────────

    /** Fired on the owning client after any replicated alignment update arrives. Use for UI refresh. */
    UPROPERTY(BlueprintAssignable, Category = "Alignment|Delegates")
    FOnAlignmentDataReplicated OnAlignmentDataReplicated;

    // ── IPersistableComponent ─────────────────────────────────────────────────

    virtual FName   GetPersistenceKey() const override { return TEXT("Alignment"); }
    virtual uint32  GetSchemaVersion()  const override { return 1; }
    virtual void    Serialize_Save(FArchive& Ar) override;
    virtual void    Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void    ClearIfSaved(uint32 FlushedGeneration) override;

    // Required dirty-tracking state (owned by this class, not the interface)
    bool   bDirty          = false;
    uint32 DirtyGeneration = 0;
    mutable TWeakObjectPtr<UPersistenceRegistrationComponent> CachedRegComp;

private:
    /** Replicated runtime data. COND_OwnerOnly — only the owning player needs their alignment. */
    UPROPERTY(ReplicatedUsing = OnRep_AlignmentData)
    FAlignmentArray AlignmentData;

    /**
     * Definition lookup — server-only.
     * Populated by RegisterAlignment. Not replicated.
     * Key = AlignmentTag.
     */
    UPROPERTY()
    TMap<FGameplayTag, TObjectPtr<UAlignmentDefinition>> Definitions;

    UFUNCTION()
    void OnRep_AlignmentData();

    void NotifyDirty();

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
        return; // Idempotent — already registered.
    }

    Definitions.Add(Tag, Definition);

    // Add a runtime entry if not already present (LoadFromSave may have seeded the value).
    if (!AlignmentData.FindByTag(Tag))
    {
        FAlignmentEntry& NewEntry    = AlignmentData.Items.AddDefaulted_GetRef();
        NewEntry.AlignmentTag        = Tag;
        NewEntry.UnderlyingValue     = 0.f;
        NewEntry.EffectiveMin        = Definition->EffectiveMin;  // Cached for client queries
        NewEntry.EffectiveMax        = Definition->EffectiveMax;
        AlignmentData.MarkItemDirty(NewEntry);
    }
    else
    {
        // Entry was pre-seeded by Serialize_Load. Ensure range is up to date.
        FAlignmentEntry* Entry  = AlignmentData.FindByTag(Tag);
        Entry->EffectiveMin     = Definition->EffectiveMin;
        Entry->EffectiveMax     = Definition->EffectiveMax;
        AlignmentData.MarkItemDirty(*Entry);
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
            UE_LOG(LogAlignment, Warning,
                TEXT("ApplyAlignmentDeltas: tag '%s' not registered on %s — skipped."),
                *Delta.AlignmentTag.ToString(), *GetOwner()->GetName());
            continue;
        }

        if (Def->ChangeRequirements)
        {
            const FRequirementResult Result = Def->ChangeRequirements->Evaluate(Context);
            if (!Result.bPassed) continue;
        }

        FAlignmentEntry* Entry = AlignmentData.FindByTag(Delta.AlignmentTag);
        if (!Entry) continue; // Should not happen if Definitions and AlignmentData are in sync.

        const float OldUnderlying  = Entry->UnderlyingValue;
        Entry->UnderlyingValue     = FMath::Clamp(
            Entry->UnderlyingValue + Delta.Delta,
            Def->SaturatedMin,
            Def->SaturatedMax);

        const float AppliedDelta = Entry->UnderlyingValue - OldUnderlying;
        if (FMath::IsNearlyZero(AppliedDelta)) continue; // Already saturated in that direction.

        AlignmentData.MarkItemDirty(*Entry);

        FAlignmentChangedEntry& ChangeEntry = Msg.Changes.AddDefaulted_GetRef();
        ChangeEntry.AlignmentTag  = Delta.AlignmentTag;
        ChangeEntry.AppliedDelta  = AppliedDelta;
        ChangeEntry.NewUnderlying = Entry->UnderlyingValue;
        ChangeEntry.NewEffective  = Entry->GetEffectiveValue();
    }

    if (Msg.Changes.IsEmpty()) return;

    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        Bus->Broadcast(
            GameCoreEventTags::Alignment_Changed,
            MoveTemp(Msg),
            EGameCoreEventScope::ServerOnly);
    }

    NotifyDirty();
}
```

---

## Query Implementations

```cpp
float UAlignmentComponent::GetEffectiveAlignment(FGameplayTag AlignmentTag) const
{
    const FAlignmentEntry* Entry = AlignmentData.FindByTag(AlignmentTag);
    if (!Entry) return 0.f;
    return Entry->GetEffectiveValue(); // Uses cached EffectiveMin/Max — works on client.
}

float UAlignmentComponent::GetUnderlyingAlignment(FGameplayTag AlignmentTag) const
{
    const FAlignmentEntry* Entry = AlignmentData.FindByTag(AlignmentTag);
    return Entry ? Entry->UnderlyingValue : 0.f;
}

bool UAlignmentComponent::IsAlignmentRegistered(FGameplayTag AlignmentTag) const
{
    return AlignmentData.FindByTag(AlignmentTag) != nullptr;
}
```

---

## Replication Setup

```cpp
void UAlignmentComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // COND_OwnerOnly: only the owning client needs their own alignment data.
    DOREPLIFETIME_CONDITION(UAlignmentComponent, AlignmentData, COND_OwnerOnly);
}

void UAlignmentComponent::OnRep_AlignmentData()
{
    // GMS is NOT broadcast on the client.
    // Game UI binds to this delegate for refresh.
    OnAlignmentDataReplicated.Broadcast();
}
```

---

## `IPersistableComponent` Implementation

```cpp
void UAlignmentComponent::Serialize_Save(FArchive& Ar)
{
    // Serialize only the tag + underlying value pairs.
    // EffectiveMin/Max is re-applied from the definition at RegisterAlignment time.
    int32 Count = AlignmentData.Items.Num();
    Ar << Count;
    for (FAlignmentEntry& Entry : AlignmentData.Items)
    {
        FGameplayTag Tag = Entry.AlignmentTag;
        Ar << Tag;
        Ar << Entry.UnderlyingValue;
    }
}

void UAlignmentComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    // Called after RegisterAlignment so Definitions is already populated.
    // Silently ignores tags not currently registered (axis removed from game).
    int32 Count;
    Ar << Count;
    for (int32 i = 0; i < Count; ++i)
    {
        FGameplayTag Tag;
        float UnderlyingValue;
        Ar << Tag;
        Ar << UnderlyingValue;

        if (FAlignmentEntry* Entry = AlignmentData.FindByTag(Tag))
        {
            Entry->UnderlyingValue = UnderlyingValue;
            AlignmentData.MarkItemDirty(*Entry);
        }
        // Tag not registered — silently skip (axis was removed from the game).
    }
}

void UAlignmentComponent::ClearIfSaved(uint32 FlushedGeneration)
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
}

void UAlignmentComponent::NotifyDirty()
{
    if (bDirty) return;
    if (!CachedRegComp.IsValid())
        CachedRegComp = GetOwner()->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (CachedRegComp.IsValid())
    {
        DirtyGeneration = CachedRegComp->SaveGeneration;
        bDirty = true;
        CachedRegComp->MarkDirty();
    }
#if !UE_BUILD_SHIPPING
    else
    {
        UE_LOG(LogAlignment, Verbose,
            TEXT("UAlignmentComponent::NotifyDirty — no UPersistenceRegistrationComponent "
                 "found on %s. Alignment changes will not be persisted."),
            *GetOwner()->GetName());
    }
#endif
}
```

> **Load Order:** `Serialize_Load` must be called **after** `RegisterAlignment` so that entries exist in `AlignmentData` to receive the restored values. The `UPersistenceSubsystem` calls load after `BeginPlay` — ensure `RegisterAlignment` calls happen in `BeginPlay` (or earlier) on the server.

---

## Important Notes

- **`Definitions` is server-only.** Clients must not call `RegisterAlignment`. The `BlueprintAuthorityOnly` specifier enforces this in Blueprint.
- **`AlignmentData` is `COND_OwnerOnly`.** Only the owning player receives their own alignment data.
- **`GetEffectiveAlignment` works on clients** because `EffectiveMin/Max` are cached on `FAlignmentEntry` at registration and replicated.
- **Thread safety.** All mutation paths are game-thread only.
- **No partial rollback.** Each axis is evaluated independently. A failing axis skips; previously changed axes in the same batch are kept.
- **Schema versioning.** Current schema is version 1. Increment `GetSchemaVersion()` and override `Migrate()` if the serialized layout changes.
