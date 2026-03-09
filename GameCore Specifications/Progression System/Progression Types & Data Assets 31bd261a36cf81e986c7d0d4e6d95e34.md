# Progression Types & Data Assets

## Overview

This page defines all shared structs, enums, and the `ULevelProgressionDefinition` data asset used by both `ULevelingComponent` and `UPointPoolComponent`. These types live in `ProgressionTypes.h` and `LevelProgressionDefinition.h`.

## File Locations

```
GameCore/Source/GameCore/Progression/
├── ProgressionTypes.h
└── LevelProgressionDefinition.h / .cpp
```

---

## ProgressionTypes.h

### EXPCurveType

```cpp
UENUM(BlueprintType)
enum class EXPCurveType : uint8
{
    Formula,     // Parametric: XP = Base * Level^Exponent with optional FRichCurve override
    CurveFloat,  // UCurveFloat asset; Level on X-axis, XP required on Y-axis
    CurveTable   // FCurveTableRowHandle; one row per progression definition
};
```

### FProgressionFormulaParams

Used when `EXPCurveType::Formula` is selected. Provides a default algebraic curve with an optional per-level multiplier override via `FRichCurve`.

```cpp
USTRUCT(BlueprintType)
struct FProgressionFormulaParams
{
    GENERATED_BODY()

    // XP = Base * (Level ^ Exponent)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
    float Base = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
    float Exponent = 1.5f;

    // Optional per-level multiplier; if keys exist they override the formula output.
    UPROPERTY(EditAnywhere, Category = "Formula")
    FRichCurve Multiplier;
};
```

### EGrantCurveType

```cpp
UENUM(BlueprintType)
enum class EGrantCurveType : uint8
{
    Constant,    // Fixed amount every level-up
    CurveFloat,  // UCurveFloat; Level on X, points granted on Y
    CurveTable   // FCurveTableRowHandle
};
```

### FProgressionGrantDefinition

Defines which point pool receives points on level-up and how many. The amount is curve-driven so grants can scale with level.

```cpp
USTRUCT(BlueprintType)
struct FProgressionGrantDefinition
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

    // Evaluates the grant amount for a given level. Called server-side only.
    int32 EvaluateForLevel(int32 Level) const;
};
```

### FProgressionPrerequisite

Fast-path prerequisite check. Requires another progression to be at a minimum level before this one can be unlocked.

```cpp
USTRUCT(BlueprintType)
struct FProgressionPrerequisite
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisite")
    FGameplayTag ProgressionTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisite")
    int32 MinLevel = 1;
};
```

### FProgressionLevelData  *(FastArray Element)*

The replicated per-progression runtime state stored in `ULevelingComponent`.

```cpp
USTRUCT()
struct FProgressionLevelData : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag ProgressionTag;

    UPROPERTY()
    int32 CurrentLevel = 0;

    // Signed: reputation progressions can lose XP within the current level floor.
    UPROPERTY()
    int32 CurrentXP = 0;

    // Callbacks for FastArray delta serialization
    void PreReplicatedRemove(const struct FProgressionLevelDataArray& InArraySerializer);
    void PostReplicatedAdd(const struct FProgressionLevelDataArray& InArraySerializer);
    void PostReplicatedChange(const struct FProgressionLevelDataArray& InArraySerializer);
};

USTRUCT()
struct FProgressionLevelDataArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FProgressionLevelData> Items;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FProgressionLevelData, FProgressionLevelDataArray>(Items, DeltaParams, *this);
    }
};
```

### FPointPoolData  *(FastArray Element)*

The replicated per-pool runtime state stored in `UPointPoolComponent`.

```cpp
USTRUCT()
struct FPointPoolData : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag PoolTag;

    UPROPERTY()
    int32 Available = 0;   // Total points ever granted into this pool

    UPROPERTY()
    int32 Consumed = 0;    // Total points spent from this pool

    UPROPERTY()
    int32 Cap = 0;         // Max allowed Available. 0 = no cap.

    int32 GetSpendable() const { return Available - Consumed; }

    bool CanAdd(int32 Amount) const
    {
        return Cap == 0 || (Available + Amount) <= Cap;
    }
};
```

---

## ULevelProgressionDefinition  (DataAsset)

One asset per progression type (e.g. `DA_Progression_CharacterLevel`, `DA_Progression_Swordsmanship`, `DA_Progression_PirateFactionRep`). Designers configure all behavior here.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API ULevelProgressionDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    // Unique tag identifying this progression (e.g. Progression.Character.Level)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FGameplayTag ProgressionTag;

    // Soft cap — can be raised per patch without code changes.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level")
    int32 MaxLevel = 100;

    // --- XP Curve ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "XP Curve")
    EXPCurveType XPCurveType = EXPCurveType::Formula;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "XPCurveType == EXPCurveType::Formula"), Category = "XP Curve")
    FProgressionFormulaParams FormulaParams;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "XPCurveType == EXPCurveType::CurveFloat"), Category = "XP Curve")
    TObjectPtr<UCurveFloat> XPCurveFloat;

    UPROPERTY(EditAnywhere, meta = (EditCondition = "XPCurveType == EXPCurveType::CurveTable"), Category = "XP Curve")
    FCurveTableRowHandle XPCurveTableRow;

    // --- Level-Up Grants ---
    // Single grant definition per progression. Amount scales with level via EGrantCurveType.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grants")
    FProgressionGrantDefinition LevelUpGrant;

    // --- Prerequisites ---
    // Fast-path: checked first, no allocations.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prerequisites")
    TArray<FProgressionPrerequisite> FastPrerequisites;

    // Advanced: full URequirement evaluation. Checked after fast prerequisites pass.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Prerequisites")
    TArray<TObjectPtr<URequirement>> AdvancedRequirements;

    // --- API ---

    // Returns XP required to reach the next level from the given level.
    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetXPRequiredForLevel(int32 Level) const;

    // Returns points to grant when leveling up TO the given level.
    UFUNCTION(BlueprintCallable, Category = "Progression")
    int32 GetGrantAmountForLevel(int32 Level) const;

    // Checks all prerequisites against the provided leveling component.
    bool ArePrerequisitesMet(const ULevelingComponent* LevelingComp, const AActor* Owner) const;

private:
    int32 EvaluateXPFormula(int32 Level) const;
};
```

## Serialization Contract

For persistence, both components expose:

```cpp
// Returns a JSON-serializable snapshot of all progression/pool state.
FString SerializeToString() const;

// Restores state from a saved snapshot. Server-only.
void DeserializeFromString(const FString& Data);
```

The game layer (not GameCore) is responsible for calling these at save/load time and forwarding data to/from the backend.