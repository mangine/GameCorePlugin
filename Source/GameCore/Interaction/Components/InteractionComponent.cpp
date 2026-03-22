// Copyright GameCore Plugin. All Rights Reserved.
#include "InteractionComponent.h"
#include "Interaction/ResolvedInteractionOption.h"
#include "Interaction/UI/InteractionIconDataAsset.h"
#include "Tags/TaggedInterface.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "InteractionSystem"

// ── ECC_GameTraceChannel_Interaction ──────────────────────────────────────────
// Project-defined custom collision channel for the interaction overlap sweep.
// Declared here for editor validation only — scanner uses it in ExecuteScan.
#ifndef ECC_GameTraceChannel_Interaction
#define ECC_GameTraceChannel_Interaction ECC_GameTraceChannel1
#endif

UInteractionComponent::UInteractionComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

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
	// Validate that all entry requirements are synchronous.
	// URequirementList::ValidateRequirements is a game-module concern; we
	// rely on ensure in each requirement's Evaluate() for dev builds.
	for (int32 i = 0; i < Total; ++i)
	{
		const FInteractionEntryConfig* Config = GetConfigAtIndex(static_cast<uint8>(i));
		if (Config && Config->EntryRequirements)
		{
			ensureMsgf(Config->EntryRequirements != nullptr,
				TEXT("[InteractionComponent] Entry %d on %s has EntryRequirements set."),
				i, *GetOwner()->GetName());
		}
	}
#endif
}

void UInteractionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Push Model: NetStates only serializes when MarkItemDirty/MarkArrayDirty is called.
	// Eliminates per-tick dirty checks at steady state across all interactable actors.
	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UInteractionComponent, NetStates, Params);
}

// ── State API ─────────────────────────────────────────────────────────────────

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

// ── Resolution ────────────────────────────────────────────────────────────────

void UInteractionComponent::ResolveOptions(
	AActor*                             SourceActor,
	AActor*                             TargetActor,
	EResolveMode                        Mode,
	TArray<FResolvedInteractionOption>& OutOptions) const
{
	OutOptions.Reset(); // Preserves heap allocation

	ITaggedInterface* SourceTags = Cast<ITaggedInterface>(SourceActor);
	ITaggedInterface* TargetTags = Cast<ITaggedInterface>(TargetActor);

	for (int32 i = 0; i < GetTotalEntryCount(); ++i)
	{
		const FInteractionEntryNetState* NetState = GetNetStateAtIndex(static_cast<uint8>(i));
		const FInteractionEntryConfig*   Config   = GetConfigAtIndex(static_cast<uint8>(i));
		if (!NetState || !Config) continue;

		// Hard skips — never show disabled or server-hidden entries.
		if (!NetState->bServerEnabled) continue;
		if (NetState->State == EInteractableState::Disabled) continue;

		EInteractableState CurrentState = NetState->State;
		FText ConditionLabel;

		// Tag pre-filters — bitset AND, near-zero cost.
		if (CurrentState != EInteractableState::Locked)
		{
			// Source required tags.
			if (Config->SourceRequiredTags.Num() > 0)
			{
				if (!SourceTags || !SourceTags->GetGameplayTags().HasAll(Config->SourceRequiredTags))
					CurrentState = EInteractableState::Locked;
			}
		}

		if (CurrentState != EInteractableState::Locked)
		{
			// Source advanced query.
			if (!Config->SourceTagQuery.IsEmpty())
			{
				if (!SourceTags || !Config->SourceTagQuery.Matches(SourceTags->GetGameplayTags()))
					CurrentState = EInteractableState::Locked;
			}
		}

		if (CurrentState != EInteractableState::Locked)
		{
			// Target required tags.
			if (Config->TargetRequiredTags.Num() > 0)
			{
				if (!TargetTags || !TargetTags->GetGameplayTags().HasAll(Config->TargetRequiredTags))
					CurrentState = EInteractableState::Locked;
			}
		}

		if (CurrentState != EInteractableState::Locked)
		{
			// Target advanced query.
			if (!Config->TargetTagQuery.IsEmpty())
			{
				if (!TargetTags || !Config->TargetTagQuery.Matches(TargetTags->GetGameplayTags()))
					CurrentState = EInteractableState::Locked;
			}
		}

		// Requirements (only if not already Locked).
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

		// Build resolved option (pointers — no copies on hot path).
		FResolvedInteractionOption Option;
		Option.SourceComponent   = const_cast<UInteractionComponent*>(this);
		Option.EntryIndex        = static_cast<uint8>(i);
		Option.Label             = &Config->Label;
		Option.InputAction       = Config->InputAction;
		Option.EntryIconOverride = Config->EntryIconOverride;
		Option.InputType         = Config->InputType;
		Option.HoldTimeSeconds   = Config->HoldTimeSeconds;
		Option.GroupTag          = Config->InteractionGroupTag;
		Option.State             = CurrentState;
		Option.ConditionLabel    = ConditionLabel;

		OutOptions.Add(Option);
	}

	// ── Exclusive check ───────────────────────────────────────────────────────
	// If any Available exclusive candidate exists, suppress all other options.
	bool bHasExclusive = false;
	int32 ExclusiveIdx = INDEX_NONE;
	int32 ExclusivePriority = INT32_MIN;

	for (int32 i = 0; i < OutOptions.Num(); ++i)
	{
		const FResolvedInteractionOption& Opt = OutOptions[i];
		const FInteractionEntryConfig* Config = GetConfigAtIndex(Opt.EntryIndex);
		if (Config && Config->bExclusive && Opt.State == EInteractableState::Available)
		{
			if (!bHasExclusive || Config->OptionPriority > ExclusivePriority)
			{
				bHasExclusive      = true;
				ExclusiveIdx       = i;
				ExclusivePriority  = Config->OptionPriority;
			}
		}
	}

	if (bHasExclusive)
	{
		FResolvedInteractionOption Winner = OutOptions[ExclusiveIdx];
		OutOptions.Reset();
		OutOptions.Add(Winner);
		return;
	}

	// ── Group resolution ──────────────────────────────────────────────────────
	if (Mode == EResolveMode::Best)
	{
		// Per GroupTag: keep only the highest-priority non-Locked entry.
		// If all entries in a group are Locked, keep the highest-priority Locked.
		TMap<FGameplayTag, int32> GroupWinner; // Tag → index in OutOptions
		for (int32 i = 0; i < OutOptions.Num(); ++i)
		{
			const FResolvedInteractionOption& Opt = OutOptions[i];
			const FInteractionEntryConfig* Config = GetConfigAtIndex(Opt.EntryIndex);
			if (!Config) continue;

			const int32* WinnerIdx = GroupWinner.Find(Opt.GroupTag);
			if (!WinnerIdx)
			{
				GroupWinner.Add(Opt.GroupTag, i);
				continue;
			}

			const FResolvedInteractionOption& Current = OutOptions[*WinnerIdx];
			const FInteractionEntryConfig* CurrentCfg = GetConfigAtIndex(Current.EntryIndex);
			if (!CurrentCfg) continue;

			// Prefer non-Locked over Locked.
			bool bCurrentLocked = (Current.State == EInteractableState::Locked);
			bool bOptLocked     = (Opt.State == EInteractableState::Locked);

			if (bCurrentLocked && !bOptLocked)
			{
				GroupWinner[Opt.GroupTag] = i;
			}
			else if (bCurrentLocked == bOptLocked && Config->OptionPriority > CurrentCfg->OptionPriority)
			{
				GroupWinner[Opt.GroupTag] = i;
			}
		}

		TArray<FResolvedInteractionOption> Filtered;
		Filtered.Reserve(GroupWinner.Num());
		for (const auto& Pair : GroupWinner)
			Filtered.Add(OutOptions[Pair.Value]);

		OutOptions = MoveTemp(Filtered);
	}
	else // All — full list sorted by (GroupTag asc, OptionPriority desc)
	{
		OutOptions.Sort([this](const FResolvedInteractionOption& A, const FResolvedInteractionOption& B)
		{
			if (A.GroupTag != B.GroupTag)
				return A.GroupTag.ToString() < B.GroupTag.ToString();

			const FInteractionEntryConfig* CfgA = GetConfigAtIndex(A.EntryIndex);
			const FInteractionEntryConfig* CfgB = GetConfigAtIndex(B.EntryIndex);
			const int32 PrioA = CfgA ? CfgA->OptionPriority : 0;
			const int32 PrioB = CfgB ? CfgB->OptionPriority : 0;
			return PrioA > PrioB;
		});
	}
}

// ── Execution ─────────────────────────────────────────────────────────────────

void UInteractionComponent::ExecuteEntry(uint8 EntryIndex, APawn* Instigator)
{
	if (!HasAuthority()) return;
	if (static_cast<int32>(EntryIndex) >= GetTotalEntryCount()) return;

	OnInteractionExecuted.Broadcast(Instigator, EntryIndex);
}

// ── Query API ─────────────────────────────────────────────────────────────────

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

// ── Rep Notify ────────────────────────────────────────────────────────────────

void UInteractionComponent::OnRep_NetStates()
{
	if (!NetStates.OwningComponent)
		NetStates.OwningComponent = this;

	// Fires on initial full-array replication for joining clients.
	// Delta updates are handled by FFastArraySerializer callbacks.
	for (const FInteractionEntryNetState& Item : NetStates.Items)
		OnEntryStateChanged.Broadcast(this, Item.EntryIndex);
}

// ── Editor Validation ─────────────────────────────────────────────────────────

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

#undef LOCTEXT_NAMESPACE
