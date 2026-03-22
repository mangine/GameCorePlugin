// Copyright GameCore Plugin. All Rights Reserved.
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
