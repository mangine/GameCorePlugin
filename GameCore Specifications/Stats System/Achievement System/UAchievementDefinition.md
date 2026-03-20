# UAchievementDefinition & Supporting Types

**Sub-page of:** [Achievement System](./Achievement%20System.md)

---

## FStatThreshold

Defines one stat condition that must be satisfied for an achievement to unlock.

```cpp
// GameCore module — AchievementTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FStatThreshold
{
    GENERATED_BODY()

    // The stat to evaluate. Must match a StatTag on a UStatDefinition.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FGameplayTag StatTag;

    // The minimum cumulative value required. Inclusive (>=).
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.0"))
    float Threshold = 1.f;
};
```

---

## FStatThresholdProgress

Read-only snapshot returned by `UAchievementComponent::GetProgress`. Used by UI to render progress bars.

```cpp
// GameCore module — AchievementTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FStatThresholdProgress
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FGameplayTag StatTag;
    UPROPERTY(BlueprintReadOnly) float Current  = 0.f;
    UPROPERTY(BlueprintReadOnly) float Required = 0.f;

    float GetNormalized() const
    {
        return Required > 0.f ? FMath::Clamp(Current / Required, 0.f, 1.f) : 1.f;
    }
};
```

---

## UAchievementDefinition

One DataAsset per achievement. Lives in the game module's content directory.

```cpp
// GameCore module — AchievementDefinition.h
UCLASS(BlueprintType)
class GAMECORE_API UAchievementDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    // Primary key. Unique across all UAchievementDefinition assets.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Achievement")
    FGameplayTag AchievementTag;

    // All thresholds must pass (AND semantics). Minimum one entry required.
    UPROPERTY(EditDefaultsOnly, Category="Achievement")
    TArray<FStatThreshold> StatThresholds;

    // Optional. Evaluated via the Requirement System watcher.
    // Null = no additional gating beyond stat thresholds.
    UPROPERTY(EditDefaultsOnly, Category="Achievement")
    TObjectPtr<URequirementList> AdditionalRequirements;
};
```

### Notes

- `StatThresholds` must not be empty. Validated at `UAchievementComponent::BeginPlay` in non-shipping builds.
- `AdditionalRequirements` authority must be `ServerOnly`. Achievements are always server-authoritative.
- A single `UAchievementDefinition` asset can be referenced by multiple `UStatDefinition.AffectedAchievements` arrays (multi-stat achievements).
- The asset carries no runtime state. All mutable state lives in `UAchievementComponent`.

---

## UStatDefinition — AffectedAchievements Addition

A new array is added to the existing `UStatDefinition` class. Soft references prevent a hard module dependency from Stats onto Achievement.

```cpp
// Addition to UStatDefinition (AchievementDefinition.h forward-declares UAchievementDefinition)

// Achievements that must be re-evaluated when this stat changes.
// Soft refs: resolved to hard refs by UAchievementComponent at BeginPlay.
// Populated by content authors, not by C++ code.
UPROPERTY(EditDefaultsOnly, Category="Achievements")
TArray<TSoftObjectPtr<UAchievementDefinition>> AffectedAchievements;
```

### Why Soft References

The Stats module must not import the Achievement module. Soft refs let `UStatDefinition` reference achievement assets without a compile-time dependency. `UAchievementComponent` resolves them at `BeginPlay` during lookup map construction and holds the hard refs for the session lifetime.

---

## FAchievementUnlockedEvent

```cpp
// GameCore module — AchievementTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FAchievementUnlockedEvent
{
    GENERATED_BODY()

    // Tag of the achievement that was just earned.
    UPROPERTY(BlueprintReadOnly) FGameplayTag AchievementTag;

    // The player who earned it.
    UPROPERTY(BlueprintReadOnly) FUniqueNetIdRepl PlayerId;
};
```

**Broadcast channel:** `GameCoreEvent.Achievement.Unlocked`  
**Scope:** Server only. Downstream systems (rewards, UI notification, audio) subscribe on the server and dispatch client RPCs as needed.

---

## Tag Declarations

```
GameCoreEvent
  Achievement
    Unlocked

RequirementEvent
  Achievement
    (none — the achievement system does not define its own invalidation events;
     it reuses RequirementEvent tags from whichever modules AdditionalRequirements reference)
```
