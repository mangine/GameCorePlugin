// Copyright GameCore Plugin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "InventoryTypes.generated.h"

class UInventoryComponent;

// ── EInventoryMutationResult ──────────────────────────────────────────────────

UENUM(BlueprintType)
enum class EInventoryMutationResult : uint8
{
	// Full quantity placed successfully.
	Success,

	// Some quantity placed; remainder could not fit.
	// Only returned by TryPlaceAuto / PlaceAuto.
	PartialSuccess,

	// Current weight + item weight exceeds MaxWeight (soft cap).
	// Blocks client-predicted placement. Server may override for authority calls.
	WeightExceeded,

	// Current weight + item weight exceeds WeightLimit (hard cap).
	// Never overridable. Placement is blocked unconditionally.
	WeightLimitExceeded,

	// No free slot available within MaxSlots (soft cap).
	SlotsExceeded,

	// Adding this item would exceed SlotLimit (hard cap).
	SlotLimitExceeded,

	// The target slot index does not exist in the layout.
	InvalidSlot,

	// The target tagged slot's URequirementList evaluated to Fail.
	RequirementFailed,

	// IItemDefinitionProvider rejected the item tag as unknown.
	// Only raised in dev builds via IsValidItem().
	InvalidItem,

	// Item quantity exceeds max stack size and no overflow slot is available.
	StackLimitExceeded,

	// No quantity could be placed (zero-quantity input or nothing fits).
	NothingToPlace,
};

// ── FInventorySlot ────────────────────────────────────────────────────────────

// The atomic unit of inventory state. Stored in FInventorySlotArray (FastArray).
USTRUCT()
struct GAMECORE_API FInventorySlot : public FFastArraySerializerItem
{
	GENERATED_BODY()

	// Stable integer identity for this slot. Assigned at creation, never reused.
	// Used as the replication delta key.
	UPROPERTY()
	int32 SlotIndex = INDEX_NONE;

	// What item occupies this slot. FGameplayTag::EmptyTag = slot is empty.
	UPROPERTY()
	FGameplayTag ItemTag;

	// Stack quantity. Always >= 1 when ItemTag is valid.
	UPROPERTY()
	int32 Quantity = 0;

	// Opaque per-instance data blob. GameCore never reads or writes this.
	// The game module owns the schema (durability, enchants, bound-to-player, etc.).
	// Serialized verbatim into the persistence archive.
	UPROPERTY()
	TArray<uint8> InstanceData;

	bool IsEmpty() const { return !ItemTag.IsValid() || Quantity <= 0; }
};

// ── FInventorySlotArray ───────────────────────────────────────────────────────

// FFastArraySerializer wrapper. Only dirty slots are sent in each replication update.
USTRUCT()
struct GAMECORE_API FInventorySlotArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FInventorySlot> Items;

	// Monotonically increasing counter; never reused. Managed by UInventoryComponent.
	int32 NextSlotIndex = 0;

	// Required by FFastArraySerializer.
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FInventorySlot,
			FInventorySlotArray>(Items, DeltaParms, *this);
	}

	// Called by FFastArraySerializer after each replicated update on the client.
	// Fires the owning UInventoryComponent's OnInventoryChanged delegate.
	// The outer UInventoryComponent* is resolved via GetTypedOuter.
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);

	// Helpers used by UInventoryComponent.
	FInventorySlot*       FindByIndex(int32 SlotIndex);
	const FInventorySlot* FindByIndex(int32 SlotIndex) const;
};

template<>
struct TStructOpsTypeTraits<FInventorySlotArray>
	: public TStructOpsTypeTraitsBase2<FInventorySlotArray>
{
	enum { WithNetDeltaSerializer = true };
};

// ── FInventoryAutoPlaceResult ─────────────────────────────────────────────────

// Returned by TryPlaceAuto and PlaceAuto. Carries both the result code and quantity accounting.
USTRUCT(BlueprintType)
struct GAMECORE_API FInventoryAutoPlaceResult
{
	GENERATED_BODY()

	// Success or PartialSuccess if any quantity was placed.
	// First blocking result code if nothing was placed.
	UPROPERTY(BlueprintReadOnly)
	EInventoryMutationResult Result = EInventoryMutationResult::NothingToPlace;

	// How many units were actually placed.
	UPROPERTY(BlueprintReadOnly)
	int32 QuantityPlaced = 0;

	// How many units could not fit. 0 on full Success.
	UPROPERTY(BlueprintReadOnly)
	int32 QuantityRemaining = 0;
};

// ── IItemDefinitionProvider ───────────────────────────────────────────────────

// Optional interface. UInventoryComponent holds a weak pointer to an implementor
// set at BeginPlay by the game module. If null, GameCore uses safe defaults
// (weight = 0, stack size = 1).
UINTERFACE(MinimalAPI, Blueprintable)
class GAMECORE_API UItemDefinitionProvider : public UInterface
{
	GENERATED_BODY()
};

class GAMECORE_API IItemDefinitionProvider
{
	GENERATED_BODY()
public:
	// Returns the weight of one unit of this item.
	// Return 0.f for weightless items. Never return negative.
	virtual float GetItemWeight(FGameplayTag ItemTag) const = 0;

	// Returns the maximum units that may occupy a single slot.
	// Return 1 for non-stackable items.
	virtual int32 GetMaxStackSize(FGameplayTag ItemTag) const = 0;

	// Returns false if the tag is not a recognised item.
	// Used in dev builds only to catch authoring errors.
	virtual bool IsValidItem(FGameplayTag ItemTag) const = 0;
};
