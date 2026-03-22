// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameFramework/PlayerState.h"
#include "Alignment/AlignmentDefinition.h"
#include "Alignment/AlignmentTypes.h"
#include "Persistence/PersistableComponent.h"
#include "Requirements/RequirementContext.h"
#include "AlignmentComponent.generated.h"

class UPersistenceRegistrationComponent;

// =============================================================================
// GMS Event Message Types
// (Message structs live in the header of the system that owns them — v2 convention)
// =============================================================================

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

// =============================================================================
// UAlignmentComponent
// =============================================================================

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
    virtual bool    IsDirty() const override { return bDirty; }

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
