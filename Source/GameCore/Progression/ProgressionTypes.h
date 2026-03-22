#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "Engine/CurveTable.h"
#include "GameFramework/PlayerState.h"
#include "ProgressionTypes.generated.h"

// ── EXPCurveType ──────────────────────────────────────────────────────────────

/**
 * Selects the XP curve mode for a ULevelProgressionDefinition.
 */
UENUM(BlueprintType)
enum class EXPCurveType : uint8
{
    Formula,    // Parametric: XP = Base * Level^Exponent with optional FRichCurve multiplier overlay
    CurveFloat, // UCurveFloat asset; Level on X-axis, XP required on Y-axis
    CurveTable  // FCurveTableRowHandle; multiple progressions sharing one asset
};

// ── FProgressionFormulaParams ─────────────────────────────────────────────────

/**
 * Used when EXPCurveType::Formula is active.
 * XP = Base * (Level ^ Exponent). Optional per-level multiplier overlay via FRichCurve.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionFormulaParams
{
    GENERATED_BODY()

    // XP = Base * (Level ^ Exponent)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
    float Base = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
    float Exponent = 1.5f;

    // Optional per-level multiplier overlay.
    // Keys on this curve override the formula output for the corresponding level.
    UPROPERTY(EditAnywhere, Category = "Formula")
    FRichCurve Multiplier;
};

// ── EGrantCurveType ───────────────────────────────────────────────────────────

/**
 * Selects how the point grant amount scales with level.
 */
UENUM(BlueprintType)
enum class EGrantCurveType : uint8
{
    Constant,   // Fixed amount every level-up
    CurveFloat, // UCurveFloat; Level on X, points granted on Y
    CurveTable  // FCurveTableRowHandle
};

// ── FProgressionGrantDefinition ───────────────────────────────────────────────

/**
 * Defines which point pool receives points on level-up and how many.
 * Amount is curve-driven so grants can scale with level.
 *
 * NOTE: Only a single grant per progression is supported (PROG-3).
 * If level-up must grant into two pools simultaneously, this must be refactored
 * to TArray<FProgressionGrantDefinition> on ULevelProgressionDefinition.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionGrantDefinition
{
    GENERATED_BODY()

    // Which point pool receives the grant (e.g. Points.Skill, Points.Attribute)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grant")
    FGameplayTag PoolTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grant")
    EGrantCurveType CurveType = EGrantCurveType::Constant;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "CurveType == EGrantCurveType::Constant"), Category = "Grant")
    int32 ConstantAmount = 1;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "CurveType == EGrantCurveType::CurveFloat"), Category = "Grant")
    TObjectPtr<UCurveFloat> GrantCurve;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "CurveType == EGrantCurveType::CurveTable"), Category = "Grant")
    FCurveTableRowHandle GrantCurveTableRow;

    // Evaluates the grant amount for a given level. Server-side only.
    int32 EvaluateForLevel(int32 Level) const;
};

// ── FProgressionPrerequisite ──────────────────────────────────────────────────

/**
 * Fast-path prerequisite: requires another progression to be at a minimum level
 * before this one can be unlocked. Checked first in RegisterProgression before
 * advancing to URequirement evaluation.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionPrerequisite
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisite")
    FGameplayTag ProgressionTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisite")
    int32 MinLevel = 1;
};

// ── FProgressionLevelData (FastArray Element) ─────────────────────────────────

struct FProgressionLevelDataArray;

/**
 * Replicated per-progression runtime state stored inside ULevelingComponent.
 * Delta-compressed per-element via FFastArraySerializer.
 */
USTRUCT()
struct GAMECORE_API FProgressionLevelData : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag ProgressionTag;

    UPROPERTY()
    int32 CurrentLevel = 0;

    // Signed: reputation progressions can lose XP within the current level floor.
    UPROPERTY()
    int32 CurrentXP = 0;

    // FastArray callbacks
    void PreReplicatedRemove(const struct FProgressionLevelDataArray& InArraySerializer);
    void PostReplicatedAdd(const struct FProgressionLevelDataArray& InArraySerializer);
    void PostReplicatedChange(const struct FProgressionLevelDataArray& InArraySerializer);
};

USTRUCT()
struct GAMECORE_API FProgressionLevelDataArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FProgressionLevelData> Items;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<
            FProgressionLevelData, FProgressionLevelDataArray>(
            Items, DeltaParams, *this);
    }
};

template <>
struct TStructOpsTypeTraits<FProgressionLevelDataArray>
    : public TStructOpsTypeTraitsBase2<FProgressionLevelDataArray>
{
    enum { WithNetDeltaSerializer = true };
};

// ── FPointPoolData (FastArray Element) ───────────────────────────────────────

struct FPointPoolDataArray;

/**
 * Replicated per-pool runtime state stored inside UPointPoolComponent.
 * Available vs Consumed accounting enables respec, audit, and rich UI.
 * Cap applies only to Available; spending is cap-free.
 */
USTRUCT()
struct GAMECORE_API FPointPoolData : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag PoolTag;

    // Total points ever granted into this pool.
    UPROPERTY()
    int32 Available = 0;

    // Total points spent from this pool.
    UPROPERTY()
    int32 Consumed = 0;

    // Max allowed Available. 0 = no cap.
    UPROPERTY()
    int32 Cap = 0;

    int32 GetSpendable() const { return Available - Consumed; }

    // Returns true if adding Amount would not exceed Cap (or Cap is 0).
    bool CanAdd(int32 Amount) const
    {
        return Cap == 0 || (Available + Amount) <= Cap;
    }

    // FastArray callbacks
    void PreReplicatedRemove(const struct FPointPoolDataArray& InArraySerializer);
    void PostReplicatedAdd(const struct FPointPoolDataArray& InArraySerializer);
    void PostReplicatedChange(const struct FPointPoolDataArray& InArraySerializer);
};

USTRUCT()
struct GAMECORE_API FPointPoolDataArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FPointPoolData> Items;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<
            FPointPoolData, FPointPoolDataArray>(
            Items, DeltaParams, *this);
    }
};

template <>
struct TStructOpsTypeTraits<FPointPoolDataArray>
    : public TStructOpsTypeTraitsBase2<FPointPoolDataArray>
{
    enum { WithNetDeltaSerializer = true };
};

// ── EPointAddResult ───────────────────────────────────────────────────────────

/**
 * Returned by UPointPoolComponent::AddPoints.
 */
UENUM(BlueprintType)
enum class EPointAddResult : uint8
{
    Success,
    PartialCap,     // Some points lost to cap — caller should log a designer warning
    PoolNotFound,
};

// ── GMS Message Structs ───────────────────────────────────────────────────────

/** Broadcast on GameCoreEvent.Progression.LevelUp */
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionLevelUpMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<APlayerState> Instigator; // The player who triggered the grant. Null for non-player grants.

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> Subject;          // The Actor that leveled up.

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag ProgressionTag;

    UPROPERTY(BlueprintReadOnly)
    int32 OldLevel = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 NewLevel = 0;
};

/** Broadcast on GameCoreEvent.Progression.XPChanged */
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionXPChangedMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<APlayerState> Instigator;

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> Subject;

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag ProgressionTag;

    UPROPERTY(BlueprintReadOnly)
    int32 NewXP = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Delta = 0;
};

/** Broadcast on GameCoreEvent.Progression.PointPoolChanged */
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionPointPoolChangedMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> Subject;

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag PoolTag;

    UPROPERTY(BlueprintReadOnly)
    int32 NewSpendable = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Delta = 0;
};

// ── Delegate Signatures ───────────────────────────────────────────────────────

// ULevelingComponent intra-system delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnProgressionLevelUp,
    FGameplayTag, ProgressionTag,
    int32, NewLevel);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnProgressionXPChanged,
    FGameplayTag, ProgressionTag,
    int32, NewXP,
    int32, Delta);

// UPointPoolComponent intra-system delegate
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPointPoolChanged,
    FGameplayTag, PoolTag,
    int32, NewSpendable,
    int32, Delta);
