// Copyright GameCore Plugin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "InteractionEnums.generated.h"

// Whether an entry requires a single press or a sustained hold to trigger.
// Drives the scanner's input handling and the hold state machine.
UENUM(BlueprintType)
enum class EInteractionInputType : uint8
{
	Press   UMETA(DisplayName = "Press"),
	Hold    UMETA(DisplayName = "Hold")
};

// The visible state of an interaction entry as seen by the client.
//
// Available / Occupied / Cooldown / Disabled are set server-side and replicated
// via FFastArraySerializer. They reflect authoritative gameplay state.
//
// Locked is the ONLY client-evaluated state. It is never replicated, never stored
// in FInteractionEntryNetState, and never set by the server. The client assigns it
// when a tag pre-filter or EntryRequirements evaluation fails during ResolveOptions.
UENUM(BlueprintType)
enum class EInteractableState : uint8
{
	Available   UMETA(DisplayName = "Available"),
	Occupied    UMETA(DisplayName = "Occupied"),    // Another actor is using this entry
	Cooldown    UMETA(DisplayName = "Cooldown"),    // Entry is temporarily unavailable
	Locked      UMETA(DisplayName = "Locked"),      // CLIENT ONLY — tag or requirement gate failed
	Disabled    UMETA(DisplayName = "Disabled")     // Administratively off — no prompt shown
};

// The reason a server-side interaction request was rejected.
// Sent to the instigating client via ClientRPC_OnInteractionRejected for UI feedback.
UENUM(BlueprintType)
enum class EInteractionRejectionReason : uint8
{
	OutOfRange       UMETA(DisplayName = "Out of Range"),
	EntryNotFound    UMETA(DisplayName = "Entry Not Found"),
	EntryUnavailable UMETA(DisplayName = "Entry Unavailable"),  // Disabled, Occupied, or Cooldown
	TagMismatch      UMETA(DisplayName = "Tag Requirement Not Met"),
	ConditionFailed  UMETA(DisplayName = "Condition Not Met")   // EntryRequirements rejected
};

// Internal hold state machine. Used privately by UInteractionManagerComponent.
// Not exposed to Blueprint or UI — drive UI via OnHoldProgressChanged / OnHoldCancelled.
UENUM()
enum class EInteractionHoldState : uint8
{
	Idle,
	Holding,
	Completed,
	Cancelled
};

// Controls how ResolveOptions shapes its output.
UENUM(BlueprintType)
enum class EResolveMode : uint8
{
	// One winner per InteractionGroupTag.
	// Highest-priority non-Locked candidate in each group.
	// Locked entries never win a group slot.
	// Use for standard HUD interaction prompts.
	Best    UMETA(DisplayName = "Best"),

	// All candidates sorted by (GroupTag asc, OptionPriority desc).
	// Locked entries included with their ConditionLabel for greyed-out display.
	// Use for inspect / examine UIs that show everything an actor offers.
	All     UMETA(DisplayName = "All")
};

// Reason a hold interaction was cancelled before completion.
UENUM(BlueprintType)
enum class EHoldCancelReason : uint8
{
	InputReleased UMETA(DisplayName = "Input Released"),
	PlayerMoved   UMETA(DisplayName = "Player Moved"),
	DisabledByTag UMETA(DisplayName = "Disabled By Tag"),
	TargetChanged UMETA(DisplayName = "Target Changed"),
	TargetLost    UMETA(DisplayName = "Target Lost"),
};
