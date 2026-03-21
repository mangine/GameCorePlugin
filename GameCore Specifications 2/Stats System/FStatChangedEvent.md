# FStatChangedEvent

**Module:** `GameCore` | **File:** `Stats/StatTypes.h`

---

## Role

Event Bus payload broadcast on `Event.Stat.Changed` every time a stat value changes. Downstream systems (Achievement, Quest, Leaderboard) subscribe to this event instead of coupling directly to `UStatComponent`.

---

## Struct Definition

```cpp
// GameCore/Source/GameCore/Stats/StatTypes.h

USTRUCT(BlueprintType)
struct GAMECORE_API FStatChangedEvent
{
    GENERATED_BODY()

    // Which stat changed.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag StatTag;

    // New cumulative value after the increment.
    UPROPERTY(BlueprintReadOnly)
    float NewValue = 0.f;

    // The increment that caused this change.
    UPROPERTY(BlueprintReadOnly)
    float Delta = 0.f;

    // The player whose stat changed.
    UPROPERTY(BlueprintReadOnly)
    FUniqueNetIdRepl PlayerId;
};
```

---

## Broadcast Details

| Field | Value |
|---|---|
| Channel tag | `Event.Stat.Changed` |
| Scope | `EGameCoreEventScope::ServerOnly` |
| Broadcast site | `UStatComponent::AddToStat` |

---

## Notes

- `PlayerId` uses `FUniqueNetIdRepl` to identify the owning player. Requires Online Subsystem. Projects without OSS should substitute with a project-specific player identity key (e.g. a GUID stored on `APlayerState`).
- `NewValue` is the post-increment cumulative total, not a snapshot from persistence. It reflects the current in-memory value.
- `Delta` is always `> 0` — `AddToStat` guards against non-positive deltas before broadcasting.
- Downstream subscribers should **filter by `StatTag` and `PlayerId`** in the listener lambda before doing any work.
