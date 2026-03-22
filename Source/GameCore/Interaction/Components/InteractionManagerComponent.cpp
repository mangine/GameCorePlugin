// Copyright GameCore Plugin. All Rights Reserved.
#include "InteractionManagerComponent.h"
#include "InteractionComponent.h"
#include "Components/HighlightComponent.h"
#include "Interaction/Data/InteractionEntryConfig.h"
#include "Interaction/Data/InteractionNetState.h"
#include "Interaction/UI/InteractionDescriptorSubsystem.h"
#include "Tags/TaggedInterface.h"
#include "Requirements/RequirementList.h"
#include "Requirements/RequirementContext.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "TimerManager.h"

// Custom trace channel for the interaction sphere sweep.
// Project must define this channel. Defaults to GameTraceChannel1 if not overridden.
#ifndef ECC_GameTraceChannel_Interaction
#define ECC_GameTraceChannel_Interaction ECC_GameTraceChannel1
#endif

// Debug CVar
static TAutoConsoleVariable<int32> CVarInteractionDebug(
	TEXT("gc.Interaction.Debug"), 0,
	TEXT("0=off | 1=scan sphere + best candidate | 2=all candidates with scores"),
	ECVF_Cheat);

UInteractionManagerComponent::UInteractionManagerComponent()
{
	// Tick is off at rest; enabled only during active hold interactions.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void UInteractionManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled())
		return;

	ScanPeriod = FMath::Max(ScanPeriod, 0.1f);
	RefreshCachedTagInterface();

	DescriptorSubsystem = GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UInteractionDescriptorSubsystem>()
		: nullptr;

	OverlapBuffer.Reserve(32);
	ResolvedBuffer.Reserve(8);

	SetComponentTickEnabled(false);

	GetWorld()->GetTimerManager().SetTimer(
		ScanTimerHandle, this,
		&UInteractionManagerComponent::ExecuteScan,
		ScanPeriod, /*bLoop=*/true, /*FirstDelay=*/0.0f);
}

void UInteractionManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
		GetWorld()->GetTimerManager().ClearTimer(ScanTimerHandle);
	ClearCurrentBest();
	Super::EndPlay(EndPlayReason);
}

// ── Tick (hold state machine) ─────────────────────────────────────────────────

void UInteractionManagerComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!HoldTargetComponent || CurrentBestComponent != HoldTargetComponent)
	{ CancelHold(EHoldCancelReason::TargetLost); return; }
	if (IsDisabledByTag())
	{ CancelHold(EHoldCancelReason::DisabledByTag); return; }

	APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn)
	{
		const float MovedSq = FVector::DistSquared(Pawn->GetActorLocation(), HoldStartLocation);
		if (MovedSq > FMath::Square(HoldCancelMoveThreshold))
		{ CancelHold(EHoldCancelReason::PlayerMoved); return; }
	}

	const FInteractionEntryConfig* Config =
		HoldTargetComponent->GetConfigAtIndex(HoldEntryIndex);
	if (!Config || Config->HoldTimeSeconds <= 0.0f)
	{ CancelHold(EHoldCancelReason::TargetLost); return; }

	HoldAccumulatedTime += DeltaTime;
	HoldProgress = FMath::Clamp(HoldAccumulatedTime / Config->HoldTimeSeconds, 0.0f, 1.0f);
	OnHoldProgressChanged.Broadcast(HoldProgress);

	if (HoldProgress >= 1.0f) CompleteHold();
}

// ── Scan Execution ────────────────────────────────────────────────────────────

void UInteractionManagerComponent::ExecuteScan()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) return;

	if (IsDisabledByTag()) { ClearCurrentBest(); return; }

	const FVector PawnLocation = Pawn->GetActorLocation();
	OverlapBuffer.Reset();
	GetWorld()->SweepMultiByChannel(
		OverlapBuffer, PawnLocation, PawnLocation,
		FQuat::Identity, ECC_GameTraceChannel_Interaction,
		FCollisionShape::MakeSphere(ScanDistance));

	// Score candidates — deduplicate by actor.
	TMap<AActor*, float>                  ActorScores;
	TMap<AActor*, UInteractionComponent*> ActorComponents;
	ActorScores.Reserve(OverlapBuffer.Num());

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	FVector CameraForward = FVector::ForwardVector;
	if (PC && PC->PlayerCameraManager)
		CameraForward = PC->PlayerCameraManager->GetCameraRotation().Vector();

	for (const FOverlapResult& Hit : OverlapBuffer)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || ActorScores.Contains(HitActor)) continue;

		UInteractionComponent* Comp =
			HitActor->FindComponentByClass<UInteractionComponent>();
		if (!Comp) continue;

		const FVector ClosestPoint = Hit.Component.IsValid()
			? Hit.Component->GetComponentLocation()
			: HitActor->GetActorLocation();

		const float Distance = FVector::Dist(ClosestPoint, PawnLocation);
		if (Distance > Comp->MaxInteractionDistance) continue;

		const float DistScore  = 1.0f - FMath::Clamp(Distance / ScanDistance, 0.0f, 1.0f);
		const FVector ToTarget = (ClosestPoint - PawnLocation).GetSafeNormal();
		const float AngleScore = (FVector::DotProduct(CameraForward, ToTarget) + 1.0f) * 0.5f;

		ActorScores.Add(HitActor, FMath::Lerp(DistScore, AngleScore, ViewAngleWeight));
		ActorComponents.Add(HitActor, Comp);
	}

	// Select winner.
	UInteractionComponent* BestComp = nullptr;
	float BestScore = -1.0f;
	for (auto& Pair : ActorScores)
		if (Pair.Value > BestScore) { BestScore = Pair.Value; BestComp = ActorComponents[Pair.Key]; }

	if (BestComp != CurrentBestComponent)
	{
		UInteractionComponent* Previous = CurrentBestComponent;

		if (CurrentBestComponent)
		{
			CurrentBestComponent->OnEntryStateChanged.RemoveDynamic(
				this, &UInteractionManagerComponent::OnTrackedStateChanged);

			if (HoldState == EInteractionHoldState::Holding &&
				HoldTargetComponent == CurrentBestComponent)
				CancelHold(EHoldCancelReason::TargetChanged);
		}

		CurrentBestComponent = BestComp;

		if (BestComp)
			BestComp->OnEntryStateChanged.AddDynamic(
				this, &UInteractionManagerComponent::OnTrackedStateChanged);

		// Update highlight.
		if (Previous && Previous->GetOwner())
			if (UHighlightComponent* H = Previous->GetOwner()->FindComponentByClass<UHighlightComponent>())
				H->SetHighlightActive(false);
		if (BestComp && BestComp->GetOwner())
			if (UHighlightComponent* H = BestComp->GetOwner()->FindComponentByClass<UHighlightComponent>())
				H->SetHighlightActive(true);

		OnBestInteractableChanged.Broadcast(Previous, BestComp);
		RunResolve();
	}
}

bool UInteractionManagerComponent::IsDisabledByTag() const
{
	if (DisablingTags.IsEmpty() || !CachedTagInterface) return false;
	return CachedTagInterface->GetGameplayTags().HasAny(DisablingTags);
}

void UInteractionManagerComponent::ClearCurrentBest()
{
	if (!CurrentBestComponent) return;

	CurrentBestComponent->OnEntryStateChanged.RemoveDynamic(
		this, &UInteractionManagerComponent::OnTrackedStateChanged);

	if (CurrentBestComponent->GetOwner())
		if (UHighlightComponent* H = CurrentBestComponent->GetOwner()->FindComponentByClass<UHighlightComponent>())
			H->SetHighlightActive(false);

	UInteractionComponent* Previous = CurrentBestComponent;
	CurrentBestComponent = nullptr;

	if (HoldState == EInteractionHoldState::Holding)
		CancelHold(EHoldCancelReason::TargetLost);

	ResolvedBuffer.Reset();
	OnBestInteractableChanged.Broadcast(Previous, nullptr);
	OnResolvedOptionsChanged.Broadcast(ResolvedBuffer);
}

void UInteractionManagerComponent::RunResolve()
{
	if (!CurrentBestComponent)
	{
		ResolvedBuffer.Reset();
		OnResolvedOptionsChanged.Broadcast(ResolvedBuffer);
		return;
	}
	CurrentBestComponent->ResolveOptions(
		GetOwner(), CurrentBestComponent->GetOwner(), ResolveMode, ResolvedBuffer);

	// Populate UIDescriptor per option.
	for (FResolvedInteractionOption& Option : ResolvedBuffer)
	{
		const FInteractionEntryConfig* Config =
			CurrentBestComponent->GetConfigAtIndex(Option.EntryIndex);
		if (Config && Config->UIDescriptorClass && DescriptorSubsystem)
			Option.UIDescriptor = DescriptorSubsystem->GetOrCreate(Config->UIDescriptorClass);
	}
	OnResolvedOptionsChanged.Broadcast(ResolvedBuffer);
}

void UInteractionManagerComponent::OnTrackedStateChanged(UInteractionComponent* /*Component*/, uint8 /*EntryIndex*/)
{
	RunResolve();
}

void UInteractionManagerComponent::RefreshCachedTagInterface()
{
	CachedTagInterface = Cast<ITaggedInterface>(GetOwner());
}

// ── Hold State Machine ────────────────────────────────────────────────────────

void UInteractionManagerComponent::BeginHold(uint8 EntryIndex)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	HoldState           = EInteractionHoldState::Holding;
	HoldEntryIndex      = EntryIndex;
	HoldProgress        = 0.0f;
	HoldAccumulatedTime = 0.0f;
	HoldStartLocation   = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	HoldTargetComponent = CurrentBestComponent;
	SetComponentTickEnabled(true);
	OnHoldProgressChanged.Broadcast(0.0f);
}

void UInteractionManagerComponent::CompleteHold()
{
	SetComponentTickEnabled(false);
	HoldState = EInteractionHoldState::Completed;
	OnHoldProgressChanged.Broadcast(1.0f);

	UInteractionComponent* Target = HoldTargetComponent;
	HoldTargetComponent = nullptr;
	HoldState = EInteractionHoldState::Idle;

	if (Target)
		ServerRequestInteract(Target, HoldEntryIndex);

	TriggerImmediateRescan();
}

void UInteractionManagerComponent::CancelHold(EHoldCancelReason Reason)
{
	if (HoldState == EInteractionHoldState::Idle) return;
	SetComponentTickEnabled(false);
	HoldTargetComponent = nullptr;
	HoldState = EInteractionHoldState::Idle;
	OnHoldProgressChanged.Broadcast(0.0f);
	OnHoldCancelled.Broadcast(HoldEntryIndex, Reason);
}

// ── Interaction Request ───────────────────────────────────────────────────────

void UInteractionManagerComponent::RequestInteract(uint8 EntryIndex)
{
	if (!CurrentBestComponent) return;

	const FInteractionEntryNetState* NetState =
		CurrentBestComponent->GetNetStateAtIndex(EntryIndex);
	if (!NetState) return;

	// Client pre-check: filter trivially rejectable states before RPC.
	if (!NetState->bServerEnabled                            ||
		NetState->State == EInteractableState::Disabled      ||
		NetState->State == EInteractableState::Occupied      ||
		NetState->State == EInteractableState::Cooldown)
		return;

	// Don't RPC for Locked resolved options.
	const FResolvedInteractionOption* Option = ResolvedBuffer.FindByPredicate(
		[EntryIndex](const FResolvedInteractionOption& O) { return O.EntryIndex == EntryIndex; });
	if (Option && Option->State == EInteractableState::Locked)
		return;

	const FInteractionEntryConfig* Config =
		CurrentBestComponent->GetConfigAtIndex(EntryIndex);
	if (!Config) return;

	if (Config->InputType == EInteractionInputType::Press)
	{
		ServerRequestInteract(CurrentBestComponent, EntryIndex);
		TriggerImmediateRescan();
	}
	else
	{
		BeginHold(EntryIndex);
	}
}

void UInteractionManagerComponent::RequestInteractRelease()
{
	if (HoldState == EInteractionHoldState::Holding)
		CancelHold(EHoldCancelReason::InputReleased);
}

void UInteractionManagerComponent::TriggerImmediateRescan()
{
	if (!GetWorld()) return;
	GetWorld()->GetTimerManager().ClearTimer(ScanTimerHandle);
	TWeakObjectPtr<UInteractionManagerComponent> WeakThis(this);
	GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
	{
		if (UInteractionManagerComponent* Mgr = WeakThis.Get())
		{
			Mgr->ExecuteScan();
			if (Mgr->GetWorld())
				Mgr->GetWorld()->GetTimerManager().SetTimer(
					Mgr->ScanTimerHandle, Mgr,
					&UInteractionManagerComponent::ExecuteScan,
					Mgr->ScanPeriod, /*bLoop=*/true);
		}
	});
}

// ── Server RPC & Validation ───────────────────────────────────────────────────

void UInteractionManagerComponent::ServerRequestInteract_Implementation(
	UInteractionComponent* ComponentRef, uint8 EntryIndex)
{
	if (!ComponentRef || !ComponentRef->GetOwner()) return;

	APawn* InstigatorPawn = Cast<APawn>(GetOwner());
	if (!InstigatorPawn) return;

	// [1] Entry exists.
	if (static_cast<int32>(EntryIndex) >= ComponentRef->GetTotalEntryCount())
	{ ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::EntryNotFound); return; }

	const FInteractionEntryNetState* NetState = ComponentRef->GetNetStateAtIndex(EntryIndex);
	const FInteractionEntryConfig*   Config   = ComponentRef->GetConfigAtIndex(EntryIndex);
	if (!NetState || !Config) return;

	// [2] Server-enabled.
	if (!NetState->bServerEnabled)
	{ ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::EntryUnavailable); return; }

	// [3] Gameplay state.
	if (NetState->State == EInteractableState::Occupied  ||
		NetState->State == EInteractableState::Cooldown  ||
		NetState->State == EInteractableState::Disabled)
	{ ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::EntryUnavailable); return; }

	// [4] Distance + 75cm latency tolerance.
	const float DistSq = FVector::DistSquared(
		InstigatorPawn->GetActorLocation(), ComponentRef->GetInteractionLocation());
	if (DistSq > FMath::Square(ComponentRef->MaxInteractionDistance + 75.0f))
	{ ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::OutOfRange); return; }

	// [5] Source tag checks.
	if (ITaggedInterface* Tags = Cast<ITaggedInterface>(InstigatorPawn))
	{
		const FGameplayTagContainer& T = Tags->GetGameplayTags();
		if (Config->SourceRequiredTags.Num() > 0 && !T.HasAll(Config->SourceRequiredTags))
		{ ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::TagMismatch); return; }
		if (!Config->SourceTagQuery.IsEmpty() && !Config->SourceTagQuery.Matches(T))
		{ ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::TagMismatch); return; }
	}

	// [6] Target tag checks.
	if (ITaggedInterface* Tags = Cast<ITaggedInterface>(ComponentRef->GetOwner()))
	{
		const FGameplayTagContainer& T = Tags->GetGameplayTags();
		if (Config->TargetRequiredTags.Num() > 0 && !T.HasAll(Config->TargetRequiredTags))
		{ ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::TagMismatch); return; }
		if (!Config->TargetTagQuery.IsEmpty() && !Config->TargetTagQuery.Matches(T))
		{ ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::TagMismatch); return; }
	}

	// [7] Entry requirements.
	if (Config->EntryRequirements)
	{
		// FRequirementContext uses FInstancedStruct Data — pack an interaction context struct.
		// Callers (game module requirements) must cast Data to their expected type.
		FRequirementContext Context;
		// Note: Instigator/PlayerState/World are not native FRequirementContext fields.
		// The game module's URequirement subclass must resolve pawn/world via subsystem lookup
		// or by casting Data to a game-module-defined context struct.
		// This is a known deviation from the spec — see DEVIATIONS.md.
		FRequirementResult Result = Config->EntryRequirements->Evaluate(Context);
		if (!Result.bPassed)
		{ ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::ConditionFailed); return; }
	}

	// All checks passed.
	ComponentRef->ExecuteEntry(EntryIndex, InstigatorPawn);         // interactable-side execution
	OnInteractionConfirmed.Broadcast(ComponentRef, EntryIndex);     // player-side systems
	ClientRPC_OnInteractionConfirmed(ComponentRef, EntryIndex);
}

void UInteractionManagerComponent::ClientRPC_OnInteractionConfirmed_Implementation(
	UInteractionComponent* Source, uint8 EntryIndex)
{
	OnInteractionConfirmedClient.Broadcast(Source, EntryIndex);
	TriggerImmediateRescan();
}

void UInteractionManagerComponent::ClientRPC_OnInteractionRejected_Implementation(
	uint8 EntryIndex, EInteractionRejectionReason Reason)
{
	OnInteractionRejected.Broadcast(EntryIndex, Reason);
	TriggerImmediateRescan();
}
