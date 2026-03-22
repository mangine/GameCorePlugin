// Copyright GameCore Plugin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/Enums/InteractionEnums.h"
#include "Interaction/ResolvedInteractionOption.h"
#include "InteractionManagerComponent.generated.h"

class UInteractionComponent;
class UInteractionDescriptorSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBestInteractableChanged,
	UInteractionComponent*, Previous,
	UInteractionComponent*, Current);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnResolvedOptionsChanged,
	const TArray<FResolvedInteractionOption>&, Options);

// SERVER only. Fires after all validation passes.
// Use for player-side reactions (animation, quest tracking, stamina costs on instigator).
// For reactions on the interactable itself, bind UInteractionComponent::OnInteractionExecuted.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionConfirmed,
	UInteractionComponent*, Source,
	uint8,                  EntryIndex);

// Owning client only. Fires after server confirms via ClientRPC.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionConfirmedClient,
	UInteractionComponent*, Source,
	uint8,                  EntryIndex);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionRejected,
	uint8,                       EntryIndex,
	EInteractionRejectionReason, Reason);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHoldProgressChanged,
	float, Progress);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHoldCancelled,
	uint8,             EntryIndex,
	EHoldCancelReason, Reason);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UInteractionManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionManagerComponent();

	// ── Scanner Configuration ─────────────────────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner")
	EResolveMode ResolveMode = EResolveMode::Best;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner",
		meta = (ClampMin = "0.1"))
	float ScanPeriod = 0.2f;

	// Global ceiling — components beyond this are never detected.
	// Must be >= the largest MaxInteractionDistance among interactables in the level.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner",
		meta = (ClampMin = "50.0"))
	float ScanDistance = 600.0f;

	// 0.0 = pure distance scoring. 1.0 = pure camera angle. 0.6 suits third-person.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ViewAngleWeight = 0.6f;

	// Scanning and hold suppressed while pawn owns any of these tags.
	// Current best cleared immediately on suppression.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner")
	FGameplayTagContainer DisablingTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner|Hold",
		meta = (ClampMin = "5.0"))
	float HoldCancelMoveThreshold = 50.0f;

	// ── Lifecycle ─────────────────────────────────────────────────────────────
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	// ── Input API (call from Enhanced Input bindings) ─────────────────────────
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void RequestInteract(uint8 EntryIndex);

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void RequestInteractRelease();

	// ── Server RPC ────────────────────────────────────────────────────────────
	UFUNCTION(Server, Reliable)
	void ServerRequestInteract(UInteractionComponent* ComponentRef, uint8 EntryIndex);

	// ── Client RPCs ───────────────────────────────────────────────────────────
	UFUNCTION(Client, Reliable)
	void ClientRPC_OnInteractionConfirmed(UInteractionComponent* Source, uint8 EntryIndex);

	UFUNCTION(Client, Reliable)
	void ClientRPC_OnInteractionRejected(uint8 EntryIndex, EInteractionRejectionReason Reason);

	// ── Delegates ─────────────────────────────────────────────────────────────
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnBestInteractableChanged OnBestInteractableChanged;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnResolvedOptionsChanged OnResolvedOptionsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractionConfirmed OnInteractionConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractionConfirmedClient OnInteractionConfirmedClient;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractionRejected OnInteractionRejected;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnHoldProgressChanged OnHoldProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnHoldCancelled OnHoldCancelled;

private:
	FTimerHandle ScanTimerHandle;

	ITaggedInterface* CachedTagInterface = nullptr;

	// Cached at BeginPlay to avoid repeated subsystem lookups in ResolveOptions.
	TObjectPtr<UInteractionDescriptorSubsystem> DescriptorSubsystem;

	TObjectPtr<UInteractionComponent> CurrentBestComponent;

	// Pre-allocated buffers — no heap allocation on the scan hot path.
	TArray<FOverlapResult>             OverlapBuffer;
	TArray<FResolvedInteractionOption> ResolvedBuffer;

	EInteractionHoldState             HoldState           = EInteractionHoldState::Idle;
	uint8                             HoldEntryIndex      = 0;
	float                             HoldProgress        = 0.0f;
	float                             HoldAccumulatedTime = 0.0f;
	FVector                           HoldStartLocation   = FVector::ZeroVector;
	TObjectPtr<UInteractionComponent> HoldTargetComponent;

	void ExecuteScan();
	void RunResolve();
	void ClearCurrentBest();
	void TriggerImmediateRescan();
	void RefreshCachedTagInterface();
	bool IsDisabledByTag() const;

	void BeginHold(uint8 EntryIndex);
	void CompleteHold();
	void CancelHold(EHoldCancelReason Reason);

	UFUNCTION()
	void OnTrackedStateChanged(UInteractionComponent* Component, uint8 EntryIndex);
};
