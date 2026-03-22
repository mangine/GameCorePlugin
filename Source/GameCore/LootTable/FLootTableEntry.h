// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Requirements/Requirement.h"
#include "FLootReward.h"
#include "FLootTableEntry.generated.h"

class ULootTable;

// ============================================================================
// ELootTableType
//
// Controls the entry selection algorithm used by ULootRollerSubsystem::RollTableInternal.
// Only Threshold is currently implemented.
// ============================================================================

UENUM(BlueprintType)
enum class ELootTableType : uint8
{
    // Roll a value in [0.0, 1.0 + LuckBonus].
    // Select the entry with the highest RollThreshold that is <= the rolled value.
    // Entries with RollThreshold > 1.0 are luck-gated: unreachable at base luck.
    // This is the only currently implemented selection model.
    Threshold,
};

// ============================================================================
// EQuantityDistribution
//
// Controls the probability distribution used when resolving a quantity within
// FLootTableEntry::Quantity range.
// ============================================================================

UENUM(BlueprintType)
enum class EQuantityDistribution : uint8
{
    // Flat random: every value in [Min, Max] equally likely.
    Uniform,

    // Bell curve: values near the midpoint of [Min, Max] are more likely.
    // Implemented as average of two uniform rolls (triangular distribution).
    // No external library dependency.
    Normal,
};

// ============================================================================
// FLootEntryReward
//
// Authoring-only. Embedded in FLootTableEntry::Reward. Holds the data designers
// configure per entry. Separate from FLootReward to keep authoring and output
// roles unambiguous.
// ============================================================================

USTRUCT(BlueprintType)
struct GAMECORE_API FLootEntryReward
{
    GENERATED_BODY()

    // Drives fulfillment routing. See FLootReward for tag examples.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag RewardType;

    // The concrete reward definition asset. Must implement ILootRewardable.
    // Enforced at authoring time by FFLootEntryRewardCustomization's filtered picker.
    // Not enforced at runtime — fulfillment layer is responsible for safe casting.
    // Null is valid for tag-only rewards where RewardType alone is sufficient.
    UPROPERTY(EditAnywhere, BlueprintReadOnly,
        meta = (GameCoreInterfaceFilter = "LootRewardable"))
    TSoftObjectPtr<UObject> RewardDefinition;
};

// ============================================================================
// FLootTableEntry
//
// Element type of ULootTable::Entries. Authored in the content browser.
// ============================================================================

USTRUCT(BlueprintType)
struct GAMECORE_API FLootTableEntry
{
    GENERATED_BODY()

    // ── Selection ────────────────────────────────────────────────────────────

    // Threshold in [0.0, inf). Entries sorted ascending by this value.
    // For Threshold mode: entry wins when rolled value >= this threshold,
    // and no higher-threshold entry also qualifies.
    // Values above 1.0 are luck-gated: only reachable when LuckBonus > (threshold - 1.0).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry",
              meta = (ClampMin = "0.0"))
    float RollThreshold = 0.0f;

    // ── Conditions ───────────────────────────────────────────────────────────

    // Optional per-entry gate evaluated against the roll instigator.
    // All requirements must pass (AND logic).
    // For OR/NOT logic use URequirement_Composite as a single array element.
    // Must contain only synchronous requirements — the roller never suspends.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Entry")
    TArray<TObjectPtr<URequirement>> EntryRequirements;

    // Controls what happens when this entry is selected but EntryRequirements fail.
    // true  — downgrade to the next lower qualifying entry that passes requirements.
    // false — no reward is granted for this roll (default).
    // Each entry has its own flag. A downgrade chain continues only while
    // consecutive downgraded entries also have this flag set.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry")
    bool bDowngradeOnRequirementFailed = false;

    // ── Reward Payload ───────────────────────────────────────────────────────

    // Authored reward data. Mutually exclusive with NestedTable.
    // If both are set, NestedTable takes priority and Reward is ignored.
    // An ensure() fires in non-shipping builds if both are configured.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward",
        meta = (EditCondition = "!NestedTable"))
    FLootEntryReward Reward;

    // If set, rolling this entry recurses into the nested table instead of
    // producing a direct reward. Counts as one recursion depth increment.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
    TSoftObjectPtr<ULootTable> NestedTable;

    // ── Quantity ─────────────────────────────────────────────────────────────

    // Quantity range. The roller samples this using QuantityDistribution
    // and writes the result into FLootReward::Quantity on output.
    // Ignored when NestedTable is set — quantity comes from sub-table entries.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quantity")
    FInt32Range Quantity = FInt32Range(1);

    // Distribution curve for quantity rolls within the range.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quantity")
    EQuantityDistribution QuantityDistribution = EQuantityDistribution::Uniform;
};

// ============================================================================
// FLootModifier
//
// Internal modifier record. Stored in ULootRollerSubsystem::Modifiers.
// Not a USTRUCT — subsystem-internal only.
// ============================================================================

struct FLootModifier
{
    FGameplayTag ContextTag;  // source tag this bonus applies to (hierarchy-matched)
    float        Bonus;       // additive roll ceiling extension, >= 0
};

// ============================================================================
// FLootModifierHandle
//
// Opaque handle returned by RegisterModifier.
// Store and pass to UnregisterModifier to remove.
// Id == 0 is the invalid sentinel.
// ============================================================================

struct GAMECORE_API FLootModifierHandle
{
    uint32 Id = 0;

    bool IsValid() const { return Id != 0; }

    bool operator==(const FLootModifierHandle& Other) const { return Id == Other.Id; }
    bool operator!=(const FLootModifierHandle& Other) const { return Id != Other.Id; }
};

FORCEINLINE uint32 GetTypeHash(const FLootModifierHandle& Handle)
{
    return GetTypeHash(Handle.Id);
}
