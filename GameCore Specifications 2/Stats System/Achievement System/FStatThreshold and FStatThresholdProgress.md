# FStatThreshold & FStatThresholdProgress

**Module:** `GameCore` | **File:** `Stats/Achievements/AchievementTypes.h`

---

## FStatThreshold

Defines one stat condition that must be satisfied for an achievement to unlock.

```cpp
// GameCore/Source/GameCore/Stats/Achievements/AchievementTypes.h

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
USTRUCT(BlueprintType)
struct GAMECORE_API FStatThresholdProgress
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FGameplayTag StatTag;
    UPROPERTY(BlueprintReadOnly) float Current  = 0.f;
    UPROPERTY(BlueprintReadOnly) float Required = 0.f;

    // Returns normalized progress [0, 1]. Safe to call with Required == 0.
    float GetNormalized() const
    {
        return Required > 0.f ? FMath::Clamp(Current / Required, 0.f, 1.f) : 1.f;
    }
};
```

---

## FAchievementUnlockedEvent

Event Bus payload broadcast on `GameCoreEvent.Achievement.Unlocked` when an achievement is earned.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FAchievementUnlockedEvent
{
    GENERATED_BODY()

    // Tag of the achievement that was just earned.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag AchievementTag;

    // The player who earned it.
    UPROPERTY(BlueprintReadOnly)
    FUniqueNetIdRepl PlayerId;
};
```

| Field | Value |
|---|---|
| Channel tag | `GameCoreEvent.Achievement.Unlocked` |
| Scope | `EGameCoreEventScope::ServerOnly` |
| Broadcast site | `UAchievementComponent::GrantAchievement` |

---

## Notes

- All three structs live in `AchievementTypes.h` to keep includes minimal.
- `FStatThreshold` and `FStatThresholdProgress` are separate types — the threshold is authored data; the progress struct is a runtime read-only snapshot.
- `FAchievementUnlockedEvent` mirrors `FStatChangedEvent` in pattern: downstream systems subscribe; they do not call into `UAchievementComponent` directly.
